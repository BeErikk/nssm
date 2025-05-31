/*******************************************************************************
 imports.cpp - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#include "nssm_pch.h"
#include "common.h"

#include "imports.h"

imports_t imports;

/*
  Try to set up function pointers.
  In this first implementation it is not an error if we can't load them
  because we aren't currently trying to load any functions which we
  absolutely need.  If we later add some indispensible imports we can
  return non-zero here to force an application exit.
*/
HMODULE get_dll(const wchar_t* dll, uint32_t* error)
{
	*error = 0;

	HMODULE ret = LoadLibrary(dll);
	if (!ret)
	{
		*error = GetLastError();
		if (*error != ERROR_PROC_NOT_FOUND)
			log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_LOADLIBRARY_FAILED, dll, error_string(*error), 0);
	}

	return ret;
}

FARPROC get_import(HMODULE library, const char* function, uint32_t* error)
{
	*error = 0;

	FARPROC ret = GetProcAddress(library, function);
	if (!ret)
	{
		*error = GetLastError();
		if (*error != ERROR_PROC_NOT_FOUND)
		{
			wchar_t* function_name;
			if (!from_utf8(function, &function_name, 0))
			{
				log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_GETPROCADDRESS_FAILED, function_name, error_string(*error), 0);
				HeapFree(GetProcessHeap(), 0, function_name);
			}
		}
	}

	return ret;
}

int32_t get_imports()
{
	uint32_t error;

	ZeroMemory(&imports, sizeof(imports));

	imports.kernel32 = get_dll(L"kernel32.dll", &error);
	if (imports.kernel32)
	{
		imports.AttachConsole = (AttachConsole_ptr)get_import(imports.kernel32, "AttachConsole", &error);
		if (!imports.AttachConsole)
		{
			if (error != ERROR_PROC_NOT_FOUND)
				return 2;
		}

		imports.QueryFullProcessImageNameW = (QueryFullProcessImageName_ptr)get_import(imports.kernel32, QUERYFULLPROCESSIMAGENAME_EXPORT, &error);
		if (!imports.QueryFullProcessImageNameW)
		{
			if (error != ERROR_PROC_NOT_FOUND)
				return 3;
		}

		imports.SleepConditionVariableCS = (SleepConditionVariableCS_ptr)get_import(imports.kernel32, "SleepConditionVariableCS", &error);
		if (!imports.SleepConditionVariableCS)
		{
			if (error != ERROR_PROC_NOT_FOUND)
				return 4;
		}

		imports.WakeConditionVariable = (WakeConditionVariable_ptr)get_import(imports.kernel32, "WakeConditionVariable", &error);
		if (!imports.WakeConditionVariable)
		{
			if (error != ERROR_PROC_NOT_FOUND)
				return 5;
		}
	}
	else if (error != ERROR_MOD_NOT_FOUND)
		return 1;

	imports.advapi32 = get_dll(L"advapi32.dll", &error);
	if (imports.advapi32)
	{
		imports.CreateWellKnownSid = (CreateWellKnownSid_ptr)get_import(imports.advapi32, "CreateWellKnownSid", &error);
		if (!imports.CreateWellKnownSid)
		{
			if (error != ERROR_PROC_NOT_FOUND)
				return 7;
		}
		imports.IsWellKnownSid = (IsWellKnownSid_ptr)get_import(imports.advapi32, "IsWellKnownSid", &error);
		if (!imports.IsWellKnownSid)
		{
			if (error != ERROR_PROC_NOT_FOUND)
				return 8;
		}
	}
	else if (error != ERROR_MOD_NOT_FOUND)
		return 6;

	return 0;
}

void free_imports()
{
	if (imports.kernel32)
		FreeLibrary(imports.kernel32);
	if (imports.advapi32)
		FreeLibrary(imports.advapi32);
	ZeroMemory(&imports, sizeof(imports));
}
