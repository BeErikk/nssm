/*******************************************************************************
 env.cpp - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#include "nssm_pch.h"
#include "common.h"

#include "env.h"

/*
  Environment block is of the form:

    KEY1=VALUE1 nullptr
    KEY2=VALUE2 nullptr
    nullptr

  A single variable KEY=VALUE has length 15:

    KEY=VALUE (13) nullptr (1)
    nullptr (1)

  Environment variable names are case-insensitive!
*/

/* Find the length in characters of an environment block. */
size_t environment_length(wchar_t* env)
{
	size_t len = 0;

	wchar_t* s;
	for (s = env;; s++)
	{
		len++;
		if (*s == '\0')
		{
			if (*(s + 1) == '\0')
			{
				len++;
				break;
			}
		}
	}

	return len;
}

/* Copy an environment block. */
wchar_t* copy_environment_block(wchar_t* env)
{
	wchar_t* newenv;
	if (copy_double_null(env, (uint32_t)environment_length(env), &newenv))
		return 0;
	return newenv;
}

/*
  The environment block starts with variables of the form
  =C:=C:\Windows\System32 which we ignore.
*/
wchar_t* useful_environment(wchar_t* rawenv)
{
	wchar_t* env = rawenv;

	if (env)
	{
		while (*env == '=')
		{
			for (; *env; env++)
				;
			env++;
		}
	}

	return env;
}

/* Expand an environment variable.  Must call HeapFree() on the result. */
wchar_t* expand_environment_string(wchar_t* string)
{
	uint32_t len;

	len = ExpandEnvironmentStrings(string, 0, 0);
	if (!len)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_EXPANDENVIRONMENTSTRINGS_FAILED, string, error_string(GetLastError()), 0);
		return 0;
	}

	wchar_t* ret = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, len * sizeof(wchar_t));
	if (!ret)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"ExpandEnvironmentStrings()", L"expand_environment_string", 0);
		return 0;
	}

	if (!ExpandEnvironmentStrings(string, ret, len))
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_EXPANDENVIRONMENTSTRINGS_FAILED, string, error_string(GetLastError()), 0);
		HeapFree(GetProcessHeap(), 0, ret);
		return 0;
	}

	return ret;
}

/*
  Set all the environment variables from an environment block in the current
  environment or remove all the variables in the block from the current
  environment.
*/
static int32_t set_environment_block(wchar_t* env, bool set)
{
	int32_t ret = 0;

	wchar_t *s, *t;
	for (s = env; *s; s++)
	{
		for (t = s; *t && *t != '='; t++)
			;
		if (*t == '=')
		{
			*t = '\0';
			if (set)
			{
				wchar_t* expanded = expand_environment_string(++t);
				if (expanded)
				{
					if (!::SetEnvironmentVariableW(s, expanded))
						ret++;
					HeapFree(GetProcessHeap(), 0, expanded);
				}
				else
				{
					if (!::SetEnvironmentVariableW(s, t))
						ret++;
				}
			}
			else
			{
				if (!::SetEnvironmentVariableW(s, nullptr))
					ret++;
			}
			for (t++; *t; t++)
				;
		}
		s = t;
	}

	return ret;
}

int32_t set_environment_block(wchar_t* env)
{
	return set_environment_block(env, true);
}

static int32_t unset_environment_block(wchar_t* env)
{
	return set_environment_block(env, false);
}

/* Remove all variables from the process environment. */
int32_t clear_environment()
{
	wchar_t* rawenv = GetEnvironmentStrings();
	wchar_t* env = useful_environment(rawenv);

	int32_t ret = unset_environment_block(env);

	if (rawenv)
		FreeEnvironmentStrings(rawenv);

	return ret;
}

/* Set the current environment to exactly duplicate an environment block. */
int32_t duplicate_environment(wchar_t* rawenv)
{
	int32_t ret = clear_environment();
	wchar_t* env = useful_environment(rawenv);
	ret += set_environment_block(env);
	return ret;
}

/*
  Verify an environment block.
  Returns:  1 if environment is invalid.
            0 if environment is OK.
           -1 on error.
*/
int32_t test_environment(wchar_t* env)
{
	wchar_t* path = (wchar_t*)nssm_imagepath();
	STARTUPINFOW si;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi;
	ZeroMemory(&pi, sizeof(pi));
	uint32_t flags = CREATE_SUSPENDED;
#ifdef UNICODE
	flags |= CREATE_UNICODE_ENVIRONMENT;
#endif

	/*
    Try to relaunch ourselves but with the candidate environment set.
    Assuming no solar flare activity, the only reason this would fail is if
    the environment were invalid.
  */
	if (::CreateProcessW(0, path, 0, 0, 0, flags, env, 0, &si, &pi))
	{
		TerminateProcess(pi.hProcess, 0);
	}
	else
	{
		uint32_t error = GetLastError();
		if (error == ERROR_INVALID_PARAMETER)
			return 1;
		else
			return -1;
	}

	return 0;
}

/*
  Duplicate an environment block returned by GetEnvironmentStrings().
  Since such a block is by definition readonly, and duplicate_environment()
  modifies its inputs, this function takes a copy of the input and operates
  on that.
*/
void duplicate_environment_strings(wchar_t* env)
{
	wchar_t* newenv = copy_environment_block(env);
	if (!newenv)
		return;

	duplicate_environment(newenv);
	HeapFree(GetProcessHeap(), 0, newenv);
}

/* Safely get a copy of the current environment. */
wchar_t* copy_environment()
{
	wchar_t* rawenv = GetEnvironmentStrings();
	if (!rawenv)
		return nullptr;
	wchar_t* env = copy_environment_block(rawenv);
	FreeEnvironmentStrings(rawenv);
	return env;
}

/*
  Create a new block with all the strings of the first block plus a new string.
  If the key is already present its value will be overwritten in place.
  If the key is blank or empty the new block will still be allocated and have
  non-zero length.
*/
int32_t append_to_environment_block(wchar_t* env, uint32_t envlen, wchar_t* string, wchar_t** newenv, uint32_t* newlen)
{
	size_t keylen = 0;
	if (string && string[0])
	{
		for (; string[keylen]; keylen++)
		{
			if (string[keylen] == _T('='))
			{
				keylen++;
				break;
			}
		}
	}
	return append_to_double_null(env, envlen, newenv, newlen, string, keylen, false);
}

/*
  Create a new block with all the strings of the first block minus the given
  string.
  If the key is not present the new block will be a copy of the original.
  If the string is KEY=VALUE the key will only be removed if its value is
  VALUE.
  If the string is just KEY the key will unconditionally be removed.
  If removing the string results in an empty list the new block will still be
  allocated and have non-zero length.
*/
int32_t remove_from_environment_block(wchar_t* env, uint32_t envlen, wchar_t* string, wchar_t** newenv, uint32_t* newlen)
{
	if (!string || !string[0] || string[0] == _T('='))
		return 1;

	wchar_t* key = 0;
	size_t len = ::wcslen(string);
	size_t i;
	for (i = 0; i < len; i++)
		if (string[i] == _T('='))
			break;

	/* Rewrite KEY to KEY= but leave KEY=VALUE alone. */
	size_t keylen = len;
	if (i == len)
		keylen++;

	key = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (keylen + 1) * sizeof(wchar_t));
	if (!key)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"key", L"remove_from_environment_block()", 0);
		return 2;
	}
	memmove(key, string, len * sizeof(wchar_t));
	if (keylen > len)
		key[keylen - 1] = _T('=');
	key[keylen] = _T('\0');

	int32_t ret = remove_from_double_null(env, envlen, newenv, newlen, key, keylen, false);
	HeapFree(GetProcessHeap(), 0, key);

	return ret;
}
