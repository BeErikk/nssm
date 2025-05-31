/*******************************************************************************
 nssm.cpp - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#include "nssm_pch.h"
#include "common.h"

#include "nssm.h"

extern uint32_t tls_index;
extern bool is_admin;
extern imports_t imports;

static wchar_t unquoted_imagepath[nssmconst::pathlength];
static wchar_t imagepath[nssmconst::pathlength];
static wchar_t imageargv0[nssmconst::pathlength];

void nssm_exit(int32_t status)
{
	free_imports();
	unsetup_utf8();
	exit(status);
}

/* Are two strings case-insensitively equivalent? */
int32_t str_equiv(const wchar_t* a, const wchar_t* b)
{
	size_t len = ::wcslen(a);
	if (::wcslen(b) != len)
		return 0;
	if (_wcsnicmp(a, b, len))
		return 0;
	return 1;
}

/* Convert a string to a number. */
int32_t str_number(const wchar_t* string, uint32_t* number, wchar_t** bogus)
{
	if (!string)
		return 1;

	*number = wcstoul(string, bogus, 0);
	if (**bogus)
		return 2;

	return 0;
}

/* User requested us to print our version. */
static bool is_version(const wchar_t* s)
{
	if (!s || !*s)
		return false;
	/* /version */
	if (*s == '/')
		s++;
	else if (*s == '-')
	{
		/* -v, -V, -version, --version */
		s++;
		if (*s == '-')
			s++;
		else if (str_equiv(s, L"v"))
			return true;
	}
	if (str_equiv(s, L"version"))
		return true;
	return false;
}

int32_t str_number(const wchar_t* string, uint32_t* number)
{
	wchar_t* bogus;
	return str_number(string, number, &bogus);
}

/* Does a char need to be escaped? */
static bool needs_escape(const wchar_t c)
{
	if (c == '"')
		return true;
	if (c == '&')
		return true;
	if (c == '%')
		return true;
	if (c == '^')
		return true;
	if (c == '<')
		return true;
	if (c == '>')
		return true;
	if (c == '|')
		return true;
	return false;
}

/* Does a char need to be quoted? */
static bool needs_quote(const wchar_t c)
{
	if (c == ' ')
		return true;
	if (c == '\t')
		return true;
	if (c == '\n')
		return true;
	if (c == '\v')
		return true;
	if (c == '"')
		return true;
	if (c == '*')
		return true;
	return needs_escape(c);
}

/* https://blogs.msdn.microsoft.com/twistylittlepassagesallalike/2011/04/23/everyone-quotes-command-line-arguments-the-wrong-way/ */
/* http://www.robvanderwoude.com/escapechars.php */
int32_t quote(const wchar_t* unquoted, wchar_t* buffer, size_t buflen)
{
	size_t i, j, n;
	size_t len = ::wcslen(unquoted);
	if (len > buflen - 1)
		return 1;

	bool escape = false;
	bool quotes = false;

	for (i = 0; i < len; i++)
	{
		if (needs_escape(unquoted[i]))
		{
			escape = quotes = true;
			break;
		}
		if (needs_quote(unquoted[i]))
			quotes = true;
	}
	if (!quotes)
	{
		memmove(buffer, unquoted, (len + 1) * sizeof(wchar_t));
		return 0;
	}

	/* "" */
	size_t quoted_len = 2;
	if (escape)
		quoted_len += 2;
	for (i = 0;; i++)
	{
		n = 0;

		while (i != len && unquoted[i] == _T('\\'))
		{
			i++;
			n++;
		}

		if (i == len)
		{
			quoted_len += n * 2;
			break;
		}
		else if (unquoted[i] == _T('"'))
			quoted_len += n * 2 + 2;
		else
			quoted_len += n + 1;
		if (needs_escape(unquoted[i]))
			quoted_len += n;
	}
	if (quoted_len > buflen - 1)
		return 1;

	wchar_t* s = buffer;
	if (escape)
		*s++ = _T('^');
	*s++ = _T('"');

	for (i = 0;; i++)
	{
		n = 0;

		while (i != len && unquoted[i] == _T('\\'))
		{
			i++;
			n++;
		}

		if (i == len)
		{
			for (j = 0; j < n * 2; j++)
			{
				if (escape)
					*s++ = _T('^');
				*s++ = _T('\\');
			}
			break;
		}
		else if (unquoted[i] == _T('"'))
		{
			for (j = 0; j < n * 2 + 1; j++)
			{
				if (escape)
					*s++ = _T('^');
				*s++ = _T('\\');
			}
			if (escape && needs_escape(unquoted[i]))
				*s++ = _T('^');
			*s++ = unquoted[i];
		}
		else
		{
			for (j = 0; j < n; j++)
			{
				if (escape)
					*s++ = _T('^');
				*s++ = _T('\\');
			}
			if (escape && needs_escape(unquoted[i]))
				*s++ = _T('^');
			*s++ = unquoted[i];
		}
	}
	if (escape)
		*s++ = _T('^');
	*s++ = _T('"');
	*s++ = _T('\0');

	return 0;
}

/* Remove basename of a path. */
void strip_basename(wchar_t* buffer)
{
	size_t len = ::wcslen(buffer);
	size_t i;
	for (i = len; i && buffer[i] != _T('\\') && buffer[i] != _T('/'); i--)
		;
	/* X:\ is OK. */
	if (i && buffer[i - 1] == _T(':'))
		i++;
	buffer[i] = _T('\0');
}

/* How to use me correctly */
int32_t usage(int32_t ret)
{
	if ((!GetConsoleWindow() || !GetStdHandle(STD_OUTPUT_HANDLE)) && ::GetProcessWindowStation())
		popup_message(0, winapi::messboxflag::ok, NSSM_MESSAGE_USAGE, NSSM_VERSION, NSSM_CONFIGURATION, NSSM_DATE);
	else
		print_message(stderr, NSSM_MESSAGE_USAGE, NSSM_VERSION, NSSM_CONFIGURATION, NSSM_DATE);
	return (ret);
}

void check_admin()
{
	is_admin = false;

	/* Lifted from MSDN examples */
	PSID AdministratorsGroup;
	SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
	if (!AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &AdministratorsGroup))
		return;
	CheckTokenMembership(0, AdministratorsGroup, /*XXX*/ (PBOOL)&is_admin);
	FreeSid(AdministratorsGroup);
}

static int32_t elevate(int32_t argc, wchar_t** argv, uint32_t message)
{
	print_message(stderr, message);

	SHELLEXECUTEINFOW sei;
	ZeroMemory(&sei, sizeof(sei));
	sei.cbSize = sizeof(sei);
	sei.lpVerb = L"runas";
	sei.lpFile = (wchar_t*)nssm_imagepath();

	wchar_t* args = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, EXE_LENGTH * sizeof(wchar_t));
	if (!args)
	{
		print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"::GetCommandLineW()", L"elevate()");
		return 111;
	}

	/* Get command line, which includes the path to NSSM, and skip that part. */
	::_snwprintf_s(args, EXE_LENGTH, _TRUNCATE, L"%s", ::GetCommandLineW());
	size_t s = ::wcslen(argv[0]) + 1;
	if (args[0] == _T('"'))
		s += 2;
	while (isspace(args[s]))
		s++;

	sei.lpParameters = args + s;
	sei.nShow = winapi::wndshowstate::show;

	uint32_t exitcode = 0;
	if (!::ShellExecuteExW(&sei))
		exitcode = 100;

	HeapFree(GetProcessHeap(), 0, (void*)args);
	return exitcode;
}

int32_t num_cpus()
{
	uintptr_t i, affinity, system_affinity;
	if (!GetProcessAffinityMask(GetCurrentProcess(), &affinity, &system_affinity))
		return 64;
	for (i = 0; system_affinity & (1LL << i); i++)
		if (i == 64)
			break;
	return (int32_t)i;
}

const wchar_t* nssm_unquoted_imagepath()
{
	return unquoted_imagepath;
}

const wchar_t* nssm_imagepath()
{
	return imagepath;
}

const wchar_t* nssm_exe()
{
	return imageargv0;
}

int32_t __cdecl wmain(int32_t argc, wchar_t** argv)
{
	if (check_console())
		setup_utf8();

	/* Remember if we are admin */
	check_admin();

	/* Set up function pointers. */
	if (get_imports())
		nssm_exit(111);

	/* Remember our path for later. */
	::_snwprintf_s(imageargv0, std::size(imageargv0), _TRUNCATE, L"%s", argv[0]);
	::PathQuoteSpacesW(imageargv0);
	::GetModuleFileNameW(0, unquoted_imagepath, std::size(unquoted_imagepath));
	::GetModuleFileNameW(0, imagepath, std::size(imagepath));
	::PathQuoteSpacesW(imagepath);

	/* Elevate */
	if (argc > 1)
	{
		/*
      Valid commands are:
      start, stop, pause, continue, install, edit, get, set, reset, unset, remove
      status, statuscode, rotate, list, processes, version
    */
		if (is_version(argv[1]))
		{
			wprintf(L"%s %s %s %s\n", NSSM, NSSM_VERSION, NSSM_CONFIGURATION, NSSM_DATE);
			nssm_exit(0);
		}
		if (str_equiv(argv[1], L"start"))
			nssm_exit(control_service(wait::controlstart, argc - 2, argv + 2));
		if (str_equiv(argv[1], L"stop"))
			nssm_exit(control_service(SERVICE_CONTROL_STOP, argc - 2, argv + 2));
		if (str_equiv(argv[1], L"restart"))
		{
			int32_t ret = control_service(SERVICE_CONTROL_STOP, argc - 2, argv + 2);
			if (ret)
				nssm_exit(ret);
			nssm_exit(control_service(wait::controlstart, argc - 2, argv + 2));
		}
		if (str_equiv(argv[1], L"pause"))
			nssm_exit(control_service(SERVICE_CONTROL_PAUSE, argc - 2, argv + 2));
		if (str_equiv(argv[1], L"continue"))
			nssm_exit(control_service(SERVICE_CONTROL_CONTINUE, argc - 2, argv + 2));
		if (str_equiv(argv[1], L"status"))
			nssm_exit(control_service(SERVICE_CONTROL_INTERROGATE, argc - 2, argv + 2));
		if (str_equiv(argv[1], L"statuscode"))
			nssm_exit(control_service(SERVICE_CONTROL_INTERROGATE, argc - 2, argv + 2, true));
		if (str_equiv(argv[1], L"rotate"))
			nssm_exit(control_service(wait::controlrotate, argc - 2, argv + 2));
		if (str_equiv(argv[1], L"install"))
		{
			if (!is_admin)
				nssm_exit(elevate(argc, argv, NSSM_MESSAGE_NOT_ADMINISTRATOR_CANNOT_INSTALL));
			create_messages();
			nssm_exit(pre_install_service(argc - 2, argv + 2));
		}
		if (str_equiv(argv[1], L"edit") || str_equiv(argv[1], L"get") || str_equiv(argv[1], L"set") || str_equiv(argv[1], L"reset") || str_equiv(argv[1], L"unset") || str_equiv(argv[1], L"dump"))
		{
			int32_t ret = pre_edit_service(argc - 1, argv + 1);
			if (ret == 3 && !is_admin && argc == 3)
				nssm_exit(elevate(argc, argv, NSSM_MESSAGE_NOT_ADMINISTRATOR_CANNOT_EDIT));
			/* There might be a password here. */
			for (int32_t i = 0; i < argc; i++)
				SecureZeroMemory(argv[i], ::wcslen(argv[i]) * sizeof(wchar_t));
			nssm_exit(ret);
		}
		if (str_equiv(argv[1], L"list"))
			nssm_exit(list_nssm_services(argc - 2, argv + 2));
		if (str_equiv(argv[1], L"processes"))
			nssm_exit(service_process_tree(argc - 2, argv + 2));
		if (str_equiv(argv[1], L"remove"))
		{
			if (!is_admin)
				nssm_exit(elevate(argc, argv, NSSM_MESSAGE_NOT_ADMINISTRATOR_CANNOT_REMOVE));
			nssm_exit(pre_remove_service(argc - 2, argv + 2));
		}
	}

	/* Thread local storage for error message buffer */
	tls_index = TlsAlloc();

	/* Register messages */
	if (is_admin)
		create_messages();

	/*
    Optimisation for Windows 2000:
    When we're run from the command line the StartServiceCtrlDispatcher() call
    will time out after a few seconds on Windows 2000.  On newer versions the
    call returns instantly.  Check for stdin first and only try to call the
    function if there's no input stream found.  Although it's possible that
    we're running with input redirected it's much more likely that we're
    actually running as a service.
    This will save time when running with no arguments from a command prompt.
  */
	if (!GetStdHandle(STD_INPUT_HANDLE))
	{
		/* Start service magic */
		SERVICE_TABLE_ENTRYW table[] = {
			{NSSM, service_main},
			{0,    0           }
        };
		if (!StartServiceCtrlDispatcher(table))
		{
			uint32_t error = GetLastError();
			/* User probably ran nssm with no argument */
			if (error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
				nssm_exit(usage(1));
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_DISPATCHER_FAILED, error_string(error), 0);
			nssm_exit(100);
		}
	}
	else
		nssm_exit(usage(1));

	/* And nothing more to do */
	nssm_exit(0);
}
