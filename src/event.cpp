/*******************************************************************************
 event.cpp - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#include "nssm_pch.h"
#include "common.h"

#include "event.h"

#define NSSM_SOURCE            L"nssm"
#define NSSM_ERROR_BUFSIZE     65535
#define NSSM_NUM_EVENT_STRINGS 16
uint32_t tls_index;

/* Convert error code to error string - must call LocalFree() on return value */
wchar_t* error_string(uint32_t error)
{
	/* Thread-safe buffer */
	wchar_t* error_message = (wchar_t*)TlsGetValue(tls_index);
	if (!error_message)
	{
		error_message = (wchar_t*)LocalAlloc(LPTR, NSSM_ERROR_BUFSIZE);
		if (!error_message)
			return L"<out of memory for error message>";
		TlsSetValue(tls_index, (void*)error_message);
	}

	if (!::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 0, error, GetUserDefaultLangID(), (wchar_t*)error_message, NSSM_ERROR_BUFSIZE, 0))
	{
		if (!::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 0, error, 0, (wchar_t*)error_message, NSSM_ERROR_BUFSIZE, 0))
		{
			if (::_snwprintf_s(error_message, NSSM_ERROR_BUFSIZE, _TRUNCATE, L"system error %u", error) < 0)
				return 0;
		}
	}
	return error_message;
}

/* Convert message code to format string */
wchar_t* message_string(uint32_t error)
{
	wchar_t* ret;
	if (!::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS, 0, error, GetUserDefaultLangID(), (PWSTR)&ret, NSSM_ERROR_BUFSIZE, 0))
	{
		if (!::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS, 0, error, 0, (PWSTR)&ret, NSSM_ERROR_BUFSIZE, 0))
		{
			ret = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, 32 * sizeof(wchar_t));
			if (::_snwprintf_s(ret, NSSM_ERROR_BUFSIZE, _TRUNCATE, L"system error %u", error) < 0)
				return 0;
		}
	}
	return ret;
}

/* Log a message to the Event Log */
void log_event(uint16_t type, uint32_t id, ...)
{
	va_list arg;
	int32_t count;
	wchar_t* s;
	wchar_t* strings[NSSM_NUM_EVENT_STRINGS];

	/* Open event log */
	HANDLE handle = ::RegisterEventSourceW(0, NSSM_SOURCE);
	if (!handle)
		return;

	/* Log it */
	count = 0;
	va_start(arg, id);
	while ((s = va_arg(arg, wchar_t*)) && count < NSSM_NUM_EVENT_STRINGS - 1)
		strings[count++] = s;
	strings[count] = 0;
	va_end(arg);
	ReportEvent(handle, type, 0, id, 0, count, 0, (const wchar_t**)strings, 0);

	/* Close event log */
	DeregisterEventSource(handle);
}

/* Log a message to the console */
void print_message(FILE* file, uint32_t id, ...)
{
	va_list arg;

	wchar_t* format = message_string(id);
	if (!format)
		return;

	va_start(arg, id);
	vfwprintf(file, format, arg);
	va_end(arg);

	LocalFree(format);
}

/* Show a GUI dialogue */
int32_t popup_message(HWND owner, uint32_t type, uint32_t id, ...)
{
	va_list arg;

	wchar_t* format = message_string(id);
	if (!format)
	{
		return ::MessageBoxW(0, L"The message which was supposed to go here is missing!", NSSM, winapi::messboxflag::ok | winapi::messboxflag::iconwarning);
	}

	wchar_t blurb[NSSM_ERROR_BUFSIZE];
	va_start(arg, id);
	if (_vsnwprintf_s(blurb, std::size(blurb), _TRUNCATE, format, arg) < 0)
	{
		va_end(arg);
		LocalFree(format);
		return ::MessageBoxW(0, L"The message which was supposed to go here is too big!", NSSM, winapi::messboxflag::ok | winapi::messboxflag::iconwarning);
	}
	va_end(arg);

	MSGBOXPARAMSW params;
	ZeroMemory(&params, sizeof(params));
	params.cbSize = sizeof(params);
	params.hInstance = ::GetModuleHandleW(0);
	params.hwndOwner = owner;
	params.lpszText = blurb;
	params.lpszCaption = NSSM;
	params.dwStyle = type;
	if (type == winapi::messboxflag::ok)
	{
		params.dwStyle |= winapi::messboxflag::usericon;
		params.lpszIcon = util::MakeEnumResourcel(IDI_NSSM);
	}

	int32_t ret = ::MessageBoxIndirectW(&params);

	LocalFree(format);

	return ret;
}
