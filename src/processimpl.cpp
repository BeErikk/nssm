/*******************************************************************************
 processimpl.cpp - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#include "nssm_pch.h"
#include "common.h"

#include "processimpl.h"

extern imports_t imports;

HANDLE get_debug_token()
{
	int32_t error;
	HANDLE token;
	if (!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, false, &token))
	{
		error = GetLastError();
		if (error == ERROR_NO_TOKEN)
		{
			(void)ImpersonateSelf(SecurityImpersonation);
			(void)OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, false, &token);
		}
	}
	if (!token)
		return INVALID_HANDLE_VALUE;

	TOKEN_PRIVILEGES privileges, old;
	ULONG size = sizeof(TOKEN_PRIVILEGES);
	LUID luid;
	if (!LookupPrivilegeValue(nullptr, SE_DEBUG_NAME, &luid))
	{
		CloseHandle(token);
		return INVALID_HANDLE_VALUE;
	}

	privileges.PrivilegeCount = 1;
	privileges.Privileges[0].Luid = luid;
	privileges.Privileges[0].Attributes = 0;

	if (!AdjustTokenPrivileges(token, false, &privileges, size, &old, &size))
	{
		CloseHandle(token);
		return INVALID_HANDLE_VALUE;
	}

	old.PrivilegeCount = 1;
	old.Privileges[0].Luid = luid;
	old.Privileges[0].Attributes |= SE_PRIVILEGE_ENABLED;

	if (!AdjustTokenPrivileges(token, false, &old, size, nullptr, nullptr))
	{
		CloseHandle(token);
		return INVALID_HANDLE_VALUE;
	}

	return token;
}

void service_kill_t(nssm_service_t* service, kill_t* k)
{
	if (!service)
		return;
	if (!k)
		return;

	ZeroMemory(k, sizeof(*k));
	k->name = service->name;
	k->process_handle = service->process_handle;
	k->pid = service->pid;
	k->exitcode = service->exitcode;
	k->stop_method = service->stop_method;
	k->kill_console_delay = service->kill_console_delay;
	k->kill_window_delay = service->kill_window_delay;
	k->kill_threads_delay = service->kill_threads_delay;
	k->status_handle = service->status_handle;
	k->status = &service->status;
	k->creation_time = service->creation_time;
	k->exit_time = service->exit_time;
}

int32_t get_process_creation_time(HANDLE process_handle, FILETIME* ft)
{
	FILETIME creation_time, exit_time, kernel_time, user_time;

	if (!GetProcessTimes(process_handle, &creation_time, &exit_time, &kernel_time, &user_time))
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GETPROCESSTIMES_FAILED, error_string(GetLastError()), 0);
		return 1;
	}

	memmove(ft, &creation_time, sizeof(creation_time));

	return 0;
}

int32_t get_process_exit_time(HANDLE process_handle, FILETIME* ft)
{
	FILETIME creation_time, exit_time, kernel_time, user_time;

	if (!GetProcessTimes(process_handle, &creation_time, &exit_time, &kernel_time, &user_time))
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GETPROCESSTIMES_FAILED, error_string(GetLastError()), 0);
		return 1;
	}

	if (!(exit_time.dwLowDateTime || exit_time.dwHighDateTime))
		return 2;
	memmove(ft, &exit_time, sizeof(exit_time));

	return 0;
}

int32_t check_parent(kill_t* k, PROCESSENTRY32* pe, uint32_t ppid)
{
	/* Check parent process ID matches. */
	if (pe->th32ParentProcessID != ppid)
		return 1;

	/*
    Process IDs can be reused so do a sanity check by making sure the child
    has been running for less time than the parent.
    Though unlikely, it's possible that the parent exited and its process ID
    was already reused, so we'll also compare against its exit time.
  */
	HANDLE process_handle = OpenProcess(PROCESS_QUERY_INFORMATION, false, pe->th32ProcessID);
	if (!process_handle)
	{
		wchar_t pid_string[16];
		::_snwprintf_s(pid_string, std::size(pid_string), _TRUNCATE, L"%lu", pe->th32ProcessID);
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENPROCESS_FAILED, pid_string, k->name, error_string(GetLastError()), 0);
		return 2;
	}

	FILETIME ft;
	if (get_process_creation_time(process_handle, &ft))
	{
		CloseHandle(process_handle);
		return 3;
	}

	CloseHandle(process_handle);

	/* Verify that the parent's creation time is not later. */
	if (CompareFileTime(&k->creation_time, &ft) > 0)
		return 4;

	/* Verify that the parent's exit time is not earlier. */
	if (CompareFileTime(&k->exit_time, &ft) < 0)
		return 5;

	return 0;
}

/* Send some window messages and hope the window respects one or more. */
int32_t CALLBACK kill_window(HWND window, LPARAM arg)
{
	kill_t* k = (kill_t*)arg;

	ULONG pid;
	if (!::GetWindowThreadProcessId(window, &pid))
		return 1;
	if (pid != k->pid)
		return 1;

	/* First try sending WM_CLOSE to request that the window close. */
	k->signalled |= ::PostMessageW(window, WM_CLOSE, k->exitcode, 0);

	/*
    Then tell the window that the user is logging off and it should exit
    without worrying about saving any data.
  */
	k->signalled |= ::PostMessageW(window, WM_ENDSESSION, 1, ENDSESSION_CLOSEAPP | ENDSESSION_CRITICAL | ENDSESSION_LOGOFF);

	return 1;
}

/*
  Try to post a message to the message queues of threads associated with the
  given process ID.  Not all threads have message queues so there's no
  guarantee of success, and we don't want to be left waiting for unsignalled
  processes so this function returns only true if at least one thread was
  successfully prodded.
*/
int32_t kill_threads(nssm_service_t* service, kill_t* k)
{
	int32_t ret = 0;

	/* Get a snapshot of all threads in the system. */
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATETOOLHELP32SNAPSHOT_THREAD_FAILED, k->name, error_string(GetLastError()), 0);
		return 0;
	}

	THREADENTRY32 te;
	ZeroMemory(&te, sizeof(te));
	te.dwSize = sizeof(te);

	if (!Thread32First(snapshot, &te))
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_THREAD_ENUMERATE_FAILED, k->name, error_string(GetLastError()), 0);
		CloseHandle(snapshot);
		return 0;
	}

	/* This thread belongs to the doomed process so signal it. */
	if (te.th32OwnerProcessID == k->pid)
	{
		ret |= ::PostThreadMessageW(te.th32ThreadID, WM_QUIT, k->exitcode, 0);
	}

	while (true)
	{
		/* Try to get the next thread. */
		if (!Thread32Next(snapshot, &te))
		{
			uint32_t error = GetLastError();
			if (error == ERROR_NO_MORE_FILES)
				break;
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_THREAD_ENUMERATE_FAILED, k->name, error_string(GetLastError()), 0);
			CloseHandle(snapshot);
			return ret;
		}

		if (te.th32OwnerProcessID == k->pid)
		{
			ret |= ::PostThreadMessageW(te.th32ThreadID, WM_QUIT, k->exitcode, 0);
		}
	}

	CloseHandle(snapshot);

	return ret;
}

int32_t kill_threads(kill_t* k)
{
	return kill_threads(nullptr, k);
}

/* Give the process a chance to die gracefully. */
int32_t kill_process(nssm_service_t* service, kill_t* k)
{
	if (!k)
		return 1;

	ULONG ret;
	if (GetExitCodeProcess(k->process_handle, &ret))
	{
		if (ret != STILL_ACTIVE)
			return 1;
	}

	/* Try to send a Control-C event to the console. */
	if (k->stop_method & stopmethod::console)
	{
		if (!kill_console(k))
			return 1;
	}

	/*
    Try to post messages to the windows belonging to the given process ID.
    If the process is a console application it won't have any windows so there's
    no guarantee of success.
  */
	if (k->stop_method & stopmethod::window)
	{
		::EnumWindows((WNDENUMPROC)kill_window, (LPARAM)k);
		if (k->signalled)
		{
			if (!await_single_handle(k->status_handle, k->status, k->process_handle, k->name, _T(__FUNCTION__), k->kill_window_delay))
				return 1;
			k->signalled = 0;
		}
	}

	/*
    Try to post messages to any thread message queues associated with the
    process.  Console applications might have them (but probably won't) so
    there's still no guarantee of success.
  */
	if (k->stop_method & stopmethod::threads)
	{
		if (kill_threads(k))
		{
			if (!await_single_handle(k->status_handle, k->status, k->process_handle, k->name, _T(__FUNCTION__), k->kill_threads_delay))
				return 1;
		}
	}

	/* We tried being nice.  Time for extreme prejudice. */
	if (k->stop_method & stopmethod::terminate)
	{
		return TerminateProcess(k->process_handle, k->exitcode);
	}

	return 0;
}

int32_t kill_process(kill_t* k)
{
	return kill_process(nullptr, k);
}

/* Simulate a Control-C event to our console (shared with the app). */
int32_t kill_console(nssm_service_t* service, kill_t* k)
{
	uint32_t ret;

	if (!k)
		return 1;

	/* Check we loaded AttachConsole(). */
	if (!imports.AttachConsole)
		return 4;

	/* Try to attach to the process's console. */
	if (!imports.AttachConsole(k->pid))
	{
		ret = GetLastError();

		switch (ret)
		{
		case ERROR_INVALID_HANDLE:
			/* The app doesn't have a console. */
			return 1;

		case ERROR_GEN_FAILURE:
			/* The app already exited. */
			return 2;

		case ERROR_ACCESS_DENIED:
		default:
			/* We already have a console. */
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_ATTACHCONSOLE_FAILED, k->name, error_string(ret), 0);
			return 3;
		}
	}

	/* Ignore the event ourselves. */
	ret = 0;
	BOOL ignored = SetConsoleCtrlHandler(0, TRUE);
	if (!ignored)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETCONSOLECTRLHANDLER_FAILED, k->name, error_string(GetLastError()), 0);
		ret = 4;
	}

	/* Send the event. */
	if (!ret)
	{
		if (!GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0))
		{
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GENERATECONSOLECTRLEVENT_FAILED, k->name, error_string(GetLastError()), 0);
			ret = 5;
		}
	}

	/* Detach from the console. */
	if (!FreeConsole())
	{
		log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_FREECONSOLE_FAILED, k->name, error_string(GetLastError()), 0);
	}

	/* Wait for process to exit. */
	if (await_single_handle(k->status_handle, k->status, k->process_handle, k->name, _T(__FUNCTION__), k->kill_console_delay))
		ret = 6;

	/* Remove our handler. */
	if (ignored && !SetConsoleCtrlHandler(0, FALSE))
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETCONSOLECTRLHANDLER_FAILED, k->name, error_string(GetLastError()), 0);
	}

	return ret;
}

int32_t kill_console(kill_t* k)
{
	return kill_console(nullptr, k);
}

void walk_process_tree(nssm_service_t* service, walk_function_t fn, kill_t* k, uint32_t ppid)
{
	if (!k)
		return;
	/* Shouldn't happen unless the service failed to start. */
	if (!k->pid)
		return; /* XXX: needed? */
	uint32_t pid = k->pid;
	uint32_t depth = k->depth;

	wchar_t pid_string[16], code[16];
	::_snwprintf_s(pid_string, std::size(pid_string), _TRUNCATE, L"%u", pid);
	::_snwprintf_s(code, std::size(code), _TRUNCATE, L"%u", k->exitcode);
	if (fn == kill_process)
		log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_KILLING, k->name, pid_string, code, 0);

	/* We will need a process handle in order to call TerminateProcess() later. */
	HANDLE process_handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_TERMINATE, false, pid);
	if (process_handle)
	{
		/* Kill this process first, then its descendents. */
		wchar_t ppid_string[16];
		::_snwprintf_s(ppid_string, std::size(ppid_string), _TRUNCATE, L"%u", ppid);
		if (fn == kill_process)
			log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_KILL_PROCESS_TREE, pid_string, ppid_string, k->name, 0);
		k->process_handle = process_handle; /* XXX: open directly? */
		if (!fn(service, k))
		{
			/* Maybe it already died. */
			uint32_t ret;
			if (!GetExitCodeProcess(process_handle, &ret) || ret == STILL_ACTIVE)
			{
				if (k->stop_method & stopmethod::terminate)
					log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_TERMINATEPROCESS_FAILED, pid_string, k->name, error_string(GetLastError()), 0);
				else
					log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_PROCESS_STILL_ACTIVE, k->name, pid_string, NSSM, regliterals::regstopmethodskip, 0);
			}
		}

		CloseHandle(process_handle);
	}
	else
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENPROCESS_FAILED, pid_string, k->name, error_string(GetLastError()), 0);

	/* Get a snapshot of all processes in the system. */
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATETOOLHELP32SNAPSHOT_PROCESS_FAILED, k->name, error_string(GetLastError()), 0);
		return;
	}

	PROCESSENTRY32 pe;
	ZeroMemory(&pe, sizeof(pe));
	pe.dwSize = sizeof(pe);

	if (!Process32First(snapshot, &pe))
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_PROCESS_ENUMERATE_FAILED, k->name, error_string(GetLastError()), 0);
		CloseHandle(snapshot);
		return;
	}

	/* This is a child of the doomed process so kill it. */
	k->depth++;
	if (!check_parent(k, &pe, pid))
	{
		k->pid = pe.th32ProcessID;
		walk_process_tree(service, fn, k, ppid);
	}
	k->pid = pid;

	while (true)
	{
		/* Try to get the next process. */
		if (!Process32Next(snapshot, &pe))
		{
			uint32_t ret = GetLastError();
			if (ret == ERROR_NO_MORE_FILES)
				break;
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_PROCESS_ENUMERATE_FAILED, k->name, error_string(GetLastError()), 0);
			CloseHandle(snapshot);
			k->depth = depth;
			return;
		}

		if (!check_parent(k, &pe, pid))
		{
			k->pid = pe.th32ProcessID;
			walk_process_tree(service, fn, k, ppid);
		}
		k->pid = pid;
	}
	k->depth = depth;

	CloseHandle(snapshot);
}

void kill_process_tree(kill_t* k, uint32_t ppid)
{
	return walk_process_tree(nullptr, kill_process, k, ppid);
}

int32_t print_process(nssm_service_t* service, kill_t* k)
{
	wchar_t exe[EXE_LENGTH];
	wchar_t* buffer = 0;
	if (k->depth)
	{
		buffer = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (k->depth + 1) * sizeof(wchar_t));
		if (buffer)
		{
			uint32_t i;
			for (i = 0; i < k->depth; i++)
				buffer[i] = _T(' ');
			buffer[i] = _T('\0');
		}
	}

	uint32_t size = std::size(exe);
	if (!imports.::QueryFullProcessImageNameW || !imports.::QueryFullProcessImageNameW(k->process_handle, 0, exe, &size))
	{
		/*
      Fall back to GetModuleFileNameEx(), which won't work for WOW64 processes.
    */
		if (!GetModuleFileNameEx(k->process_handle, nullptr, exe, std::size(exe)))
		{
			int32_t error = GetLastError();
			if (error == ERROR_PARTIAL_COPY)
				::_snwprintf_s(exe, std::size(exe), _TRUNCATE, L"[WOW64]");
			else
				::_snwprintf_s(exe, std::size(exe), _TRUNCATE, L"???");
		}
	}

	wprintf(L"% 8lu %s%s\n", k->pid, buffer ? buffer : L"", exe);

	if (buffer)
		HeapFree(GetProcessHeap(), 0, buffer);
	return 1;
}

int32_t print_process(kill_t* k)
{
	return print_process(nullptr, k);
}
