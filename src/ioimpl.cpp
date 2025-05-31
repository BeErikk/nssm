/*******************************************************************************
 ioimpl.cpp - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#include "nssm_pch.h"
#include "common.h"

#include "ioimpl.h"

#define COMPLAINED_READ   (1 << 0)
#define COMPLAINED_WRITE  (1 << 1)
#define COMPLAINED_ROTATE (1 << 2)
#define TIMESTAMP_FORMAT  "%04u-%02u-%02u %02u:%02u:%02u.%03u: "
#define TIMESTAMP_LEN     25

static int32_t dup_handle(HANDLE source_handle, HANDLE* dest_handle_ptr, wchar_t* source_description, wchar_t* dest_description, uint32_t flags)
{
	if (!dest_handle_ptr)
		return 1;

	if (!DuplicateHandle(GetCurrentProcess(), source_handle, GetCurrentProcess(), dest_handle_ptr, 0, true, flags))
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_DUPLICATEHANDLE_FAILED, source_description, dest_description, error_string(GetLastError()), 0);
		return 2;
	}
	return 0;
}

static int32_t dup_handle(HANDLE source_handle, HANDLE* dest_handle_ptr, wchar_t* source_description, wchar_t* dest_description)
{
	return dup_handle(source_handle, dest_handle_ptr, source_description, dest_description, DUPLICATE_SAME_ACCESS);
}

/*
  read_handle:  read from application
  pipe_handle:  stdout of application
  write_handle: to file
*/
static HANDLE create_logging_thread(wchar_t* service_name, wchar_t* path, uint32_t sharing, uint32_t disposition, uint32_t flags, HANDLE* read_handle_ptr, HANDLE* pipe_handle_ptr, HANDLE* write_handle_ptr, uint32_t rotate_bytes_low, uint32_t rotate_bytes_high, uint32_t rotate_delay, uint32_t* tid_ptr, uint32_t* rotate_online, bool timestamp_log, bool copy_and_truncate)
{
	*tid_ptr = 0;

	/* Pipe between application's stdout/stderr and our logging handle. */
	if (read_handle_ptr && !*read_handle_ptr)
	{
		if (pipe_handle_ptr && !*pipe_handle_ptr)
		{
			if (CreatePipe(read_handle_ptr, pipe_handle_ptr, 0, 0))
			{
				SetHandleInformation(*pipe_handle_ptr, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
			}
			else
			{
				log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEPIPE_FAILED, service_name, path, error_string(GetLastError()));
				return (HANDLE)0;
			}
		}
	}

	logger_t* logger = (logger_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(logger_t));
	if (!logger)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"logger", L"create_logging_thread()", 0);
		return (HANDLE)0;
	}

	ULARGE_INTEGER size;
	size.LowPart = rotate_bytes_low;
	size.HighPart = rotate_bytes_high;

	logger->service_name = service_name;
	logger->path = path;
	logger->sharing = sharing;
	logger->disposition = disposition;
	logger->flags = flags;
	logger->read_handle = *read_handle_ptr;
	logger->write_handle = *write_handle_ptr;
	logger->size = (int64_t)size.QuadPart;
	logger->tid_ptr = tid_ptr;
	logger->timestamp_log = timestamp_log;
	logger->line_length = 0;
	logger->rotate_online = rotate_online;
	logger->rotate_delay = rotate_delay;
	logger->copy_and_truncate = copy_and_truncate;

	HANDLE thread_handle = CreateThread(nullptr, 0, log_and_rotate, (void*)logger, 0, logger->tid_ptr);
	if (!thread_handle)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATETHREAD_FAILED, error_string(GetLastError()), 0);
		HeapFree(GetProcessHeap(), 0, logger);
	}

	return thread_handle;
}

static inline uint32_t guess_charsize(void* address, uint32_t bufsize)
{
	if (IsTextUnicode(address, bufsize, 0))
		return (uint32_t)sizeof(wchar_t);
	else
		return (uint32_t)sizeof(char);
}

static inline void write_bom(logger_t* logger, uint32_t* out)
{
	wchar_t bom = L'\ufeff';
	if (!WriteFile(logger->write_handle, (void*)&bom, sizeof(bom), out, 0))
	{
		log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_SOMEBODY_SET_UP_US_THE_BOM, logger->service_name, logger->path, error_string(GetLastError()), 0);
	}
}

void close_handle(HANDLE* handle, HANDLE* remember)
{
	if (remember)
		*remember = INVALID_HANDLE_VALUE;
	if (!handle)
		return;
	if (!*handle)
		return;
	CloseHandle(*handle);
	if (remember)
		*remember = *handle;
	*handle = 0;
}

void close_handle(HANDLE* handle)
{
	close_handle(handle, nullptr);
}

/* Get path, share mode, creation disposition and flags for a stream. */
int32_t get_createfile_parameters(HKEY key, wchar_t* prefix, wchar_t* path, uint32_t* sharing, uint32_t default_sharing, uint32_t* disposition, uint32_t default_disposition, uint32_t* flags, uint32_t default_flags, bool* copy_and_truncate)
{
	wchar_t value[std::to_underlying(registry_const::stdiolength)];

	/* Path. */
	if (::_snwprintf_s(value, std::size(value), _TRUNCATE, L"%s", prefix) < 0)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, prefix, L"get_createfile_parameters()", 0);
		return 1;
	}
	switch (expand_parameter(key, value, path, nssmconst::pathlength, true, false))
	{
	case 0:
		if (!path[0])
			return 0;
		break; /* OK. */
	default:
		return 2; /* Error. */
	}

	/* ShareMode. */
	if (::_snwprintf_s(value, std::size(value), _TRUNCATE, L"%s%s", prefix, regliterals::regsharemode) < 0)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, regliterals::regsharemode, L"get_createfile_parameters()", 0);
		return 3;
	}
	switch (get_number(key, value, sharing, false))
	{
	case 0:
		*sharing = default_sharing;
		break; /* Missing. */
	case 1:
		break; /* Found. */
	case -2:
		return 4; /* Error. */
	}

	/* CreationDisposition. */
	if (::_snwprintf_s(value, std::size(value), _TRUNCATE, L"%s%s", prefix, regliterals::regdisposition) < 0)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, regliterals::regdisposition, L"get_createfile_parameters()", 0);
		return 5;
	}
	switch (get_number(key, value, disposition, false))
	{
	case 0:
		*disposition = default_disposition;
		break; /* Missing. */
	case 1:
		break; /* Found. */
	case -2:
		return 6; /* Error. */
	}

	/* Flags. */
	if (::_snwprintf_s(value, std::size(value), _TRUNCATE, L"%s%s", prefix, regliterals::regstdioflags) < 0)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, regliterals::regstdioflags, L"get_createfile_parameters()", 0);
		return 7;
	}
	switch (get_number(key, value, flags, false))
	{
	case 0:
		*flags = default_flags;
		break; /* Missing. */
	case 1:
		break; /* Found. */
	case -2:
		return 8; /* Error. */
	}

	/* Rotate with ::CopyFileW() and SetEndOfFile(). */
	if (copy_and_truncate)
	{
		uint32_t data;
		if (::_snwprintf_s(value, std::size(value), _TRUNCATE, L"%s%s", prefix, regliterals::regcopytruncate) < 0)
		{
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, regliterals::regcopytruncate, L"get_createfile_parameters()", 0);
			return 9;
		}
		switch (get_number(key, value, &data, false))
		{
		case 0:
			*copy_and_truncate = false;
			break; /* Missing. */
		case 1: /* Found. */
			if (data)
				*copy_and_truncate = true;
			else
				*copy_and_truncate = false;
			break;
		case -2:
			return 9; /* Error. */
		}
	}

	return 0;
}

int32_t set_createfile_parameter(HKEY key, wchar_t* prefix, wchar_t* suffix, uint32_t number)
{
	wchar_t value[std::to_underlying(registry_const::stdiolength)];

	if (::_snwprintf_s(value, std::size(value), _TRUNCATE, L"%s%s", prefix, suffix) < 0)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, suffix, L"set_createfile_parameter()", 0);
		return 1;
	}

	return set_number(key, value, number);
}

int32_t delete_createfile_parameter(HKEY key, wchar_t* prefix, wchar_t* suffix)
{
	wchar_t value[std::to_underlying(registry_const::stdiolength)];

	if (::_snwprintf_s(value, std::size(value), _TRUNCATE, L"%s%s", prefix, suffix) < 0)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, suffix, L"delete_createfile_parameter()", 0);
		return 1;
	}

	if (::RegDeleteValueW(key, value))
		return 0;
	return 1;
}

HANDLE write_to_file(wchar_t* path, uint32_t sharing, SECURITY_ATTRIBUTES* attributes, uint32_t disposition, uint32_t flags)
{
	static LARGE_INTEGER offset = {0};
	HANDLE ret = ::CreateFileW(path, FILE_WRITE_DATA, sharing, attributes, disposition, flags, 0);
	if (ret != INVALID_HANDLE_VALUE)
	{
		if (SetFilePointerEx(ret, offset, 0, FILE_END))
			SetEndOfFile(ret);
		return ret;
	}

	log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEFILE_FAILED, path, error_string(GetLastError()), 0);
	return ret;
}

static void rotated_filename(wchar_t* path, wchar_t* rotated, uint32_t rotated_len, SYSTEMTIME* st)
{
	if (!st)
	{
		SYSTEMTIME now;
		st = &now;
		GetSystemTime(st);
	}

	wchar_t buffer[nssmconst::pathlength];
	memmove(buffer, path, sizeof(buffer));
	wchar_t* ext = ::PathFindExtensionW(buffer);
	wchar_t extension[nssmconst::pathlength];
	::_snwprintf_s(extension, std::size(extension), _TRUNCATE, L"-%04u%02u%02uT%02u%02u%02u.%03u%s", st->wYear, st->wMonth, st->wDay, st->wHour, st->wMinute, st->wSecond, st->wMilliseconds, ext);
	*ext = _T('\0');
	::_snwprintf_s(rotated, rotated_len, _TRUNCATE, L"%s%s", buffer, extension);
}

void rotate_file(wchar_t* service_name, wchar_t* path, uint32_t seconds, uint32_t delay, uint32_t low, uint32_t high, bool copy_and_truncate)
{
	uint32_t error;

	/* Now. */
	SYSTEMTIME st;
	GetSystemTime(&st);

	BY_HANDLE_FILE_INFORMATION info;

	/* Try to open the file to check if it exists and to get attributes. */
	HANDLE file = ::CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (file != INVALID_HANDLE_VALUE)
	{
		/* Get file attributes. */
		if (!GetFileInformationByHandle(file, &info))
		{
			/* Reuse current time for rotation timestamp. */
			seconds = low = high = 0;
			SystemTimeToFileTime(&st, &info.ftLastWriteTime);
		}

		CloseHandle(file);
	}
	else
	{
		error = GetLastError();
		if (error == ERROR_FILE_NOT_FOUND)
			return;
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_ROTATE_FILE_FAILED, service_name, path, L"::CreateFileW()", path, error_string(error), 0);
		/* Reuse current time for rotation timestamp. */
		seconds = low = high = 0;
		SystemTimeToFileTime(&st, &info.ftLastWriteTime);
	}

	/* Check file age. */
	if (seconds)
	{
		FILETIME ft;
		SystemTimeToFileTime(&st, &ft);

		ULARGE_INTEGER s;
		s.LowPart = ft.dwLowDateTime;
		s.HighPart = ft.dwHighDateTime;
		s.QuadPart -= seconds * 10000000LL;
		ft.dwLowDateTime = s.LowPart;
		ft.dwHighDateTime = s.HighPart;
		if (CompareFileTime(&info.ftLastWriteTime, &ft) > 0)
			return;
	}

	/* Check file size. */
	if (low || high)
	{
		if (info.nFileSizeHigh < high)
			return;
		if (info.nFileSizeHigh == high && info.nFileSizeLow < low)
			return;
	}

	/* Get new filename. */
	FileTimeToSystemTime(&info.ftLastWriteTime, &st);

	wchar_t rotated[nssmconst::pathlength];
	rotated_filename(path, rotated, std::size(rotated), &st);

	/* Rotate. */
	bool ok = true;
	wchar_t* function;
	if (copy_and_truncate)
	{
		function = L"::CopyFileW()";
		if (::CopyFileW(path, rotated, TRUE))
		{
			file = write_to_file(path, NSSM_STDOUT_SHARING, 0, NSSM_STDOUT_DISPOSITION, NSSM_STDOUT_FLAGS);
			Sleep(delay);
			SetFilePointer(file, 0, 0, FILE_BEGIN);
			SetEndOfFile(file);
			CloseHandle(file);
		}
		else
			ok = false;
	}
	else
	{
		function = L"::MoveFileW()";
		if (!::MoveFileW(path, rotated))
			ok = false;
	}
	if (ok)
	{
		log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_ROTATED, service_name, path, rotated, 0);
		return;
	}
	error = GetLastError();

	if (error == ERROR_FILE_NOT_FOUND)
		return;
	log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_ROTATE_FILE_FAILED, service_name, path, function, rotated, error_string(error), 0);
	return;
}

int32_t get_output_handles(nssm_service_t* service, STARTUPINFOW* si)
{
	if (!si)
		return 1;
	bool inherit_handles = false;

	/* Allocate a new console so we get a fresh stdin, stdout and stderr. */
	alloc_console(service);

	/* stdin */
	if (service->stdin_path[0])
	{
		si->hStdInput = ::CreateFileW(service->stdin_path, FILE_READ_DATA, service->stdin_sharing, 0, service->stdin_disposition, service->stdin_flags, 0);
		if (si->hStdInput == INVALID_HANDLE_VALUE)
		{
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEFILE_FAILED, service->stdin_path, error_string(GetLastError()), 0);
			return 2;
		}

		inherit_handles = true;
	}

	/* stdout */
	if (service->stdout_path[0])
	{
		if (service->rotate_files)
			rotate_file(service->name, service->stdout_path, service->rotate_seconds, service->rotate_bytes_low, service->rotate_bytes_high, service->rotate_delay, service->stdout_copy_and_truncate);
		HANDLE stdout_handle = write_to_file(service->stdout_path, service->stdout_sharing, 0, service->stdout_disposition, service->stdout_flags);
		if (stdout_handle == INVALID_HANDLE_VALUE)
			return 4;
		service->stdout_si = 0;

		if (service->use_stdout_pipe)
		{
			service->stdout_pipe = si->hStdOutput = 0;
			service->stdout_thread = create_logging_thread(service->name, service->stdout_path, service->stdout_sharing, service->stdout_disposition, service->stdout_flags, &service->stdout_pipe, &service->stdout_si, &stdout_handle, service->rotate_bytes_low, service->rotate_bytes_high, service->rotate_delay, &service->stdout_tid, &service->rotate_stdout_online, service->timestamp_log, service->stdout_copy_and_truncate);
			if (!service->stdout_thread)
			{
				CloseHandle(service->stdout_pipe);
				CloseHandle(service->stdout_si);
			}
		}
		else
			service->stdout_thread = 0;

		if (!service->stdout_thread)
		{
			if (dup_handle(stdout_handle, &service->stdout_si, regliterals::regstdout, L"stdout", DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS))
				return 4;
			service->rotate_stdout_online = NSSM_ROTATE_OFFLINE;
		}

		if (dup_handle(service->stdout_si, &si->hStdOutput, L"stdout_si", L"stdout"))
			close_handle(&service->stdout_thread);

		inherit_handles = true;
	}

	/* stderr */
	if (service->stderr_path[0])
	{
		/* Same as stdout? */
		if (str_equiv(service->stderr_path, service->stdout_path))
		{
			service->stderr_sharing = service->stdout_sharing;
			service->stderr_disposition = service->stdout_disposition;
			service->stderr_flags = service->stdout_flags;
			service->rotate_stderr_online = NSSM_ROTATE_OFFLINE;

			/* Two handles to the same file will create a race. */
			/* XXX: Here we assume that either both or neither handle must be a pipe. */
			if (dup_handle(service->stdout_si, &service->stderr_si, L"stdout", L"stderr"))
				return 6;
		}
		else
		{
			if (service->rotate_files)
				rotate_file(service->name, service->stderr_path, service->rotate_seconds, service->rotate_bytes_low, service->rotate_bytes_high, service->rotate_delay, service->stderr_copy_and_truncate);
			HANDLE stderr_handle = write_to_file(service->stderr_path, service->stderr_sharing, 0, service->stderr_disposition, service->stderr_flags);
			if (stderr_handle == INVALID_HANDLE_VALUE)
				return 7;
			service->stderr_si = 0;

			if (service->use_stderr_pipe)
			{
				service->stderr_pipe = si->hStdError = 0;
				service->stderr_thread = create_logging_thread(service->name, service->stderr_path, service->stderr_sharing, service->stderr_disposition, service->stderr_flags, &service->stderr_pipe, &service->stderr_si, &stderr_handle, service->rotate_bytes_low, service->rotate_bytes_high, service->rotate_delay, &service->stderr_tid, &service->rotate_stderr_online, service->timestamp_log, service->stderr_copy_and_truncate);
				if (!service->stderr_thread)
				{
					CloseHandle(service->stderr_pipe);
					CloseHandle(service->stderr_si);
				}
			}
			else
				service->stderr_thread = 0;

			if (!service->stderr_thread)
			{
				if (dup_handle(stderr_handle, &service->stderr_si, regliterals::regstderr, L"stderr", DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS))
					return 7;
				service->rotate_stderr_online = NSSM_ROTATE_OFFLINE;
			}
		}

		if (dup_handle(service->stderr_si, &si->hStdError, L"stderr_si", L"stderr"))
			close_handle(&service->stderr_thread);

		inherit_handles = true;
	}

	/*
    We need to set the startup_info flags to make the new handles
    inheritable by the new process.
  */
	if (inherit_handles)
		si->dwFlags |= STARTF_USESTDHANDLES;

	return 0;
}

/* Reuse output handles for a hook. */
int32_t use_output_handles(nssm_service_t* service, STARTUPINFOW* si)
{
	si->dwFlags &= ~STARTF_USESTDHANDLES;

	if (service->stdout_si)
	{
		if (dup_handle(service->stdout_si, &si->hStdOutput, L"stdout_pipe", L"hStdOutput"))
			return 1;
		si->dwFlags |= STARTF_USESTDHANDLES;
	}

	if (service->stderr_si)
	{
		if (dup_handle(service->stderr_si, &si->hStdError, L"stderr_pipe", L"hStdError"))
		{
			if (si->hStdOutput)
			{
				si->dwFlags &= ~STARTF_USESTDHANDLES;
				CloseHandle(si->hStdOutput);
			}
			return 2;
		}
		si->dwFlags |= STARTF_USESTDHANDLES;
	}

	return 0;
}

void close_output_handles(STARTUPINFOW* si)
{
	if (si->hStdInput)
		CloseHandle(si->hStdInput);
	if (si->hStdOutput)
		CloseHandle(si->hStdOutput);
	if (si->hStdError)
		CloseHandle(si->hStdError);
}

void cleanup_loggers(nssm_service_t* service)
{
	uint32_t interval = wait::cleanupdeadline;
	HANDLE thread_handle = INVALID_HANDLE_VALUE;

	close_handle(&service->stdout_thread, &thread_handle);
	/* Close write end of the data pipe so logging thread can finalise read. */
	close_handle(&service->stdout_si);
	/* Await logging thread then close read end. */
	if (thread_handle != INVALID_HANDLE_VALUE)
		WaitForSingleObject(thread_handle, interval);
	close_handle(&service->stdout_pipe);

	thread_handle = INVALID_HANDLE_VALUE;
	close_handle(&service->stderr_thread, &thread_handle);
	close_handle(&service->stderr_si);
	if (thread_handle != INVALID_HANDLE_VALUE)
		WaitForSingleObject(thread_handle, interval);
	close_handle(&service->stderr_pipe);
}

/*
  Try multiple times to read from a file.
  Returns:  0 on success.
            1 on non-fatal error.
           -1 on fatal error.
*/
static int32_t try_read(logger_t* logger, void* address, uint32_t bufsize, uint32_t* in, int32_t* complained)
{
	int32_t ret = 1;
	uint32_t error;
	for (int32_t tries = 0; tries < 5; tries++)
	{
		if (ReadFile(logger->read_handle, address, bufsize, in, 0))
			return 0;

		error = GetLastError();
		switch (error)
		{
		/* Other end closed the pipe. */
		case ERROR_BROKEN_PIPE:
			ret = -1;
			goto complain_read;

		/* Couldn't lock the buffer. */
		case ERROR_NOT_ENOUGH_QUOTA:
			Sleep(2000 + tries * 3000);
			ret = 1;
			continue;

		/* Write was cancelled by the other end. */
		case ERROR_OPERATION_ABORTED:
			ret = 1;
			goto complain_read;

		default:
			ret = -1;
		}
	}

complain_read:
	/* Ignore the error if we've been requested to exit anyway. */
	if (*logger->rotate_online != NSSM_ROTATE_ONLINE)
		return ret;
	if (!(*complained & COMPLAINED_READ))
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_READFILE_FAILED, logger->service_name, logger->path, error_string(error), 0);
	*complained |= COMPLAINED_READ;
	return ret;
}

/*
  Try multiple times to write to a file.
  Returns:  0 on success.
            1 on non-fatal error.
           -1 on fatal error.
*/
static int32_t try_write(logger_t* logger, void* address, uint32_t bufsize, uint32_t* out, int32_t* complained)
{
	int32_t ret = 1;
	uint32_t error;
	for (int32_t tries = 0; tries < 5; tries++)
	{
		if (WriteFile(logger->write_handle, address, bufsize, out, 0))
			return 0;

		error = GetLastError();
		if (error == ERROR_IO_PENDING)
		{
			/* Operation was successful pending flush to disk. */
			return 0;
		}

		switch (error)
		{
		/* Other end closed the pipe. */
		case ERROR_BROKEN_PIPE:
			ret = -1;
			goto complain_write;

		/* Couldn't lock the buffer. */
		case ERROR_NOT_ENOUGH_QUOTA:
		/* Out of disk space. */
		case ERROR_DISK_FULL:
			Sleep(2000 + tries * 3000);
			ret = 1;
			continue;

		default:
			/* We'll lose this line but try to read and write subsequent ones. */
			ret = 1;
		}
	}

complain_write:
	if (!(*complained & COMPLAINED_WRITE))
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_WRITEFILE_FAILED, logger->service_name, logger->path, error_string(error), 0);
	*complained |= COMPLAINED_WRITE;
	return ret;
}

/* Note that the timestamp is created in UTF-8. */
static inline int32_t write_timestamp(logger_t* logger, uint32_t charsize, uint32_t* out, int32_t* complained)
{
	char timestamp[TIMESTAMP_LEN + 1];

	SYSTEMTIME now;
	GetSystemTime(&now);
	_snprintf_s(timestamp, std::size(timestamp), _TRUNCATE, TIMESTAMP_FORMAT, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);

	if (charsize == sizeof(char))
		return try_write(logger, (void*)timestamp, TIMESTAMP_LEN, out, complained);

	wchar_t* utf16;
	uint32_t utf16len;
	if (to_utf16(timestamp, &utf16, &utf16len))
		return -1;
	int32_t ret = try_write(logger, (void*)*utf16, utf16len * sizeof(wchar_t), out, complained);
	HeapFree(GetProcessHeap(), 0, utf16);
	return ret;
}

static int32_t write_with_timestamp(logger_t* logger, void* address, uint32_t bufsize, uint32_t* out, int32_t* complained, uint32_t charsize)
{
	if (logger->timestamp_log)
	{
		uint32_t log_out;
		int32_t log_complained;
		uint32_t timestamp_out = 0;
		int32_t timestamp_complained;
		if (!logger->line_length)
		{
			write_timestamp(logger, charsize, &timestamp_out, &timestamp_complained);
			logger->line_length += (int64_t)timestamp_out;
			*out += timestamp_out;
			*complained |= timestamp_complained;
		}

		uint32_t i;
		void* line = address;
		uint32_t offset = 0;
		int32_t ret;
		for (i = 0; i < bufsize; i++)
		{
			if (((char*)address)[i] == '\n')
			{
				ret = try_write(logger, line, i - offset + 1, &log_out, &log_complained);
				line = (void*)((char*)line + i - offset + 1);
				logger->line_length = 0LL;
				*out += log_out;
				*complained |= log_complained;
				offset = i + 1;
				if (offset < bufsize)
				{
					write_timestamp(logger, charsize, &timestamp_out, &timestamp_complained);
					logger->line_length += (int64_t)timestamp_out;
					*out += timestamp_out;
					*complained |= timestamp_complained;
				}
			}
		}

		if (offset < bufsize)
		{
			ret = try_write(logger, line, bufsize - offset, &log_out, &log_complained);
			*out += log_out;
			*complained |= log_complained;
		}

		return ret;
	}
	else
		return try_write(logger, address, bufsize, out, complained);
}

/* Wrapper to be called in a new thread for logging. */
ULONG __stdcall log_and_rotate(void* arg)
{
	logger_t* logger = (logger_t*)arg;
	if (!logger)
		return 1;

	int64_t size;
	BY_HANDLE_FILE_INFORMATION info;

	/* Find initial file size. */
	if (!GetFileInformationByHandle(logger->write_handle, &info))
		logger->size = 0LL;
	else
	{
		ULARGE_INTEGER l;
		l.HighPart = info.nFileSizeHigh;
		l.LowPart = info.nFileSizeLow;
		size = l.QuadPart;
	}

	char buffer[1024];
	void* address;
	uint32_t in, out;
	uint32_t charsize = 0;
	uint32_t error;
	int32_t ret;
	int32_t complained = 0;

	while (true)
	{
		/* Read data from the pipe. */
		address = &buffer;
		ret = try_read(logger, address, sizeof(buffer), &in, &complained);
		if (ret < 0)
		{
			close_handle(&logger->read_handle);
			close_handle(&logger->write_handle);
			HeapFree(GetProcessHeap(), 0, logger);
			return 2;
		}
		else if (ret)
			continue;

		if (*logger->rotate_online == NSSM_ROTATE_ONLINE_ASAP || (logger->size && size + (int64_t)in >= logger->size))
		{
			/* Look for newline. */
			uint32_t i;
			for (i = 0; i < in; i++)
			{
				if (buffer[i] == '\n')
				{
					if (!charsize)
						charsize = guess_charsize(address, in);
					i += charsize;

					/* Write up to the newline. */
					ret = try_write(logger, address, i, &out, &complained);
					if (ret < 0)
					{
						close_handle(&logger->read_handle);
						close_handle(&logger->write_handle);
						HeapFree(GetProcessHeap(), 0, logger);
						return 3;
					}
					size += (int64_t)out;

					/* Rotate. */
					*logger->rotate_online = NSSM_ROTATE_ONLINE;
					wchar_t rotated[nssmconst::pathlength];
					rotated_filename(logger->path, rotated, std::size(rotated), 0);

					/*
            Ideally we'd try the rename first then close the handle but
            ::MoveFileW() will fail if the handle is still open so we must
            risk losing everything.
          */
					if (logger->copy_and_truncate)
						FlushFileBuffers(logger->write_handle);
					close_handle(&logger->write_handle);
					bool ok = true;
					wchar_t* function;
					if (logger->copy_and_truncate)
					{
						function = L"::CopyFileW()";
						if (::CopyFileW(logger->path, rotated, TRUE))
						{
							HANDLE file = write_to_file(logger->path, NSSM_STDOUT_SHARING, 0, NSSM_STDOUT_DISPOSITION, NSSM_STDOUT_FLAGS);
							Sleep(logger->rotate_delay);
							SetFilePointer(file, 0, 0, FILE_BEGIN);
							SetEndOfFile(file);
							CloseHandle(file);
						}
						else
							ok = false;
					}
					else
					{
						function = L"::MoveFileW()";
						if (!::MoveFileW(logger->path, rotated))
							ok = false;
					}
					if (ok)
					{
						log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_ROTATED, logger->service_name, logger->path, rotated, 0);
						size = 0LL;
					}
					else
					{
						error = GetLastError();
						if (error != ERROR_FILE_NOT_FOUND)
						{
							if (!(complained & COMPLAINED_ROTATE))
								log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_ROTATE_FILE_FAILED, logger->service_name, logger->path, function, rotated, error_string(error), 0);
							complained |= COMPLAINED_ROTATE;
							/* We can at least try to re-open the existing file. */
							logger->disposition = OPEN_ALWAYS;
						}
					}

					/* Reopen. */
					logger->write_handle = write_to_file(logger->path, logger->sharing, 0, logger->disposition, logger->flags);
					if (logger->write_handle == INVALID_HANDLE_VALUE)
					{
						error = GetLastError();
						log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEFILE_FAILED, logger->path, error_string(error), 0);
						/* Oh dear.  Now we can't log anything further. */
						close_handle(&logger->read_handle);
						close_handle(&logger->write_handle);
						HeapFree(GetProcessHeap(), 0, logger);
						return 4;
					}

					/* Resume writing after the newline. */
					address = (void*)((char*)address + i);
					in -= i;
				}
			}
		}

		if (!size || logger->timestamp_log)
			if (!charsize)
				charsize = guess_charsize(address, in);
		if (!size)
		{
			/* Write a BOM to the new file. */
			if (charsize == sizeof(wchar_t))
				write_bom(logger, &out);
			size += (int64_t)out;
		}

		/* Write the data, if any. */
		if (!in)
			continue;

		ret = write_with_timestamp(logger, address, in, &out, &complained, charsize);
		size += (int64_t)out;
		if (ret < 0)
		{
			close_handle(&logger->read_handle);
			close_handle(&logger->write_handle);
			HeapFree(GetProcessHeap(), 0, logger);
			return 3;
		}
	}

	close_handle(&logger->read_handle);
	close_handle(&logger->write_handle);
	HeapFree(GetProcessHeap(), 0, logger);
	return 0;
}
