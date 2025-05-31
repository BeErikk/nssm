/*******************************************************************************
 settings.cpp - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#include "nssm_pch.h"
#include "common.h"

#include "settings.h"
/* XXX: (value && value->string) is probably bogus because value is probably never null */

// clang-format off
namespace locals
{
constexpr std::wstring_view all         {L"All"};       /* Affinity - NSSM_AFFINITY_ALL */
constexpr std::wstring_view defvalue    {L"Default"};   /* Default value - NSSM_DEFAULT_STRING */
}
// clang-format on

extern const wchar_t* exit_action_strings[];
extern const wchar_t* startup_strings[];
extern const wchar_t* priority_strings[];
extern const wchar_t* hook_event_strings[];
extern const wchar_t* hook_action_strings[];

/* Does the parameter refer to the default value of the setting? */
static inline int32_t is_default(const wchar_t* value)
{
	return (str_equiv(value, locals::defvalue.data()) || str_equiv(value, L"*") || !value[0]);
}

/* What type of parameter is it parameter? */
static inline bool is_string_type(const uint32_t type)
{
	return (type == REG_MULTI_SZ || type == REG_EXPAND_SZ || type == REG_SZ);
}
static inline bool is_numeric_type(const uint32_t type)
{
	return (type == REG_DWORD);
}

static int32_t value_from_string(const wchar_t* name, value_t* value, const wchar_t* string)
{
	size_t len = ::wcslen(string);
	if (!len++)
	{
		value->string = 0;
		return 0;
	}

	value->string = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, len * sizeof(wchar_t));
	if (!value->string)
	{
		print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, name, L"value_from_string()");
		return -1;
	}

	if (::_snwprintf_s(value->string, len, _TRUNCATE, L"%s", string) < 0)
	{
		HeapFree(GetProcessHeap(), 0, value->string);
		print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, name, L"value_from_string()");
		return -1;
	}

	return 1;
}

/* Functions to manage NSSM-specific settings in the registry. */
static int32_t setting_set_number(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value,[[maybe_unused]] const wchar_t* additional)
{
	HKEY key = (HKEY)param;
	if (!key)
		return -1;

	uint32_t number;
	LSTATUS status;

	/* Resetting to default? */
	if (!value || !value->string)
	{
		status = ::RegDeleteValueW(key, name);
		if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND)
			return 0;
		print_message(stderr, NSSM_MESSAGE_REGDELETEVALUE_FAILED, name, service_name, error_string(status));
		return -1;
	}
	if (str_number(value->string, &number))
		return -1;

	if (default_value && number == PtrToUlong(default_value))
	{
		status = ::RegDeleteValueW(key, name);
		if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND)
			return 0;
		print_message(stderr, NSSM_MESSAGE_REGDELETEVALUE_FAILED, name, service_name, error_string(status));
		return -1;
	}

	if (set_number(key, (wchar_t*)name, number))
		return -1;

	return 1;
}

static int32_t setting_get_number([[maybe_unused]] const wchar_t* service_name, void* param, const wchar_t* name,[[maybe_unused]] void* default_value, value_t* value,[[maybe_unused]] const wchar_t* additional)
{
	HKEY key = (HKEY)param;
	return get_number(key, (wchar_t*)name, &value->numeric, false);
}

static int32_t setting_set_string(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value,[[maybe_unused]] const wchar_t* additional)
{
	HKEY key = (HKEY)param;
	if (!key)
		return -1;

	LSTATUS status;

	/* Resetting to default? */
	if (!value || !value->string)
	{
		if (default_value)
			value->string = (wchar_t*)default_value;
		else
		{
			status = ::RegDeleteValueW(key, name);
			if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND)
				return 0;
			print_message(stderr, NSSM_MESSAGE_REGDELETEVALUE_FAILED, name, service_name, error_string(status));
			return -1;
		}
	}
	if (default_value && ::wcslen((wchar_t*)default_value) && str_equiv(value->string, (wchar_t*)default_value))
	{
		status = ::RegDeleteValueW(key, name);
		if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND)
			return 0;
		print_message(stderr, NSSM_MESSAGE_REGDELETEVALUE_FAILED, name, service_name, error_string(status));
		return -1;
	}

	if (set_expand_string(key, (wchar_t*)name, value->string))
		return -1;

	return 1;
}

static int32_t setting_get_string([[maybe_unused]] const wchar_t* service_name, void* param, const wchar_t* name,[[maybe_unused]] void* default_value, value_t* value,[[maybe_unused]] const wchar_t* additional)
{
	HKEY key = (HKEY)param;
	wchar_t buffer[VALUE_LENGTH];

	if (get_string(key, (wchar_t*)name, (wchar_t*)buffer, (uint32_t)sizeof(buffer), false, false, false))
		return -1;

	return value_from_string(name, value, buffer);
}

static int32_t setting_not_dumpable([[maybe_unused]]const wchar_t* service_name,[[maybe_unused]] void* param,[[maybe_unused]] const wchar_t* name,[[maybe_unused]] void* default_value,[[maybe_unused]] value_t* value,[[maybe_unused]] const wchar_t* additional)
{
	return 0;
}

static int32_t setting_dump_string(const wchar_t* service_name, void* param, const wchar_t* name, const value_t* value, const wchar_t* additional)
{
	wchar_t quoted_service_name[SERVICE_NAME_LENGTH * 2];
	wchar_t quoted_value[VALUE_LENGTH * 2];
	wchar_t quoted_additional[VALUE_LENGTH * 2];
	wchar_t quoted_nssm[EXE_LENGTH * 2];

	if (quote(service_name, quoted_service_name, std::size(quoted_service_name)))
		return 1;

	if (additional)
	{
		if (::wcslen(additional))
		{
			if (quote(additional, quoted_additional, std::size(quoted_additional)))
				return 3;
		}
		else
			::_snwprintf_s(quoted_additional, std::size(quoted_additional), _TRUNCATE, L"\"\"");
	}
	else
		quoted_additional[0] = '\0';

	uint32_t type = (uint32_t)param;
	if (is_string_type(type))
	{
		if (::wcslen(value->string))
		{
			if (quote(value->string, quoted_value, std::size(quoted_value)))
				return 2;
		}
		else
			::_snwprintf_s(quoted_value, std::size(quoted_value), _TRUNCATE, L"\"\"");
	}
	else if (is_numeric_type(type))
		::_snwprintf_s(quoted_value, std::size(quoted_value), _TRUNCATE, L"%u", value->numeric);
	else
		return 2;

	if (quote(nssm_exe(), quoted_nssm, std::size(quoted_nssm)))
		return 3;
	if (::wcslen(quoted_additional))
		wprintf(L"%s set %s %s %s %s\n", quoted_nssm, quoted_service_name, name, quoted_additional, quoted_value);
	else
		wprintf(L"%s set %s %s %s\n", quoted_nssm, quoted_service_name, name, quoted_value);
	return 0;
}

static int32_t setting_set_exit_action(const wchar_t* service_name,[[maybe_unused]] void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	uint32_t exitcode;
	wchar_t* code;
	wchar_t action_string[ACTION_LEN];

	if (additional)
	{
		/* Default action? */
		if (is_default(additional))
			code = 0;
		else
		{
			if (str_number(additional, &exitcode))
				return -1;
			code = (wchar_t*)additional;
		}
	}

	HKEY key = open_registry(service_name, name, KEY_WRITE);
	if (!key)
		return -1;

	int32_t error;
	int32_t ret = 1;

	/* Resetting to default? */
	if (value && value->string)
		::_snwprintf_s(action_string, std::size(action_string), _TRUNCATE, L"%s", value->string);
	else
	{
		if (code)
		{
			/* Delete explicit action. */
			error = ::RegDeleteValueW(key, code);
			RegCloseKey(key);
			if (error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND)
				return 0;
			print_message(stderr, NSSM_MESSAGE_REGDELETEVALUE_FAILED, code, service_name, error_string(error));
			return -1;
		}
		else
		{
			/* Explicitly keep the default action. */
			if (default_value)
				::_snwprintf_s(action_string, std::size(action_string), _TRUNCATE, L"%s", (wchar_t*)default_value);
			ret = 0;
		}
	}

	/* Validate the string. */
	for (int32_t i = 0; exit_action_strings[i]; i++)
	{
		if (!_wcsnicmp((const wchar_t*)action_string, exit_action_strings[i], ACTION_LEN))
		{
			if (default_value && str_equiv(action_string, (wchar_t*)default_value))
				ret = 0;
			if (::RegSetValueExW(key, code, 0, REG_SZ, (const uint8_t*)exit_action_strings[i], (uint32_t)(::wcslen(action_string) + 1) * sizeof(wchar_t)) != ERROR_SUCCESS)
			{
				print_message(stderr, NSSM_MESSAGE_SETVALUE_FAILED, code, service_name, error_string(GetLastError()));
				RegCloseKey(key);
				return -1;
			}

			RegCloseKey(key);
			return ret;
		}
	}

	print_message(stderr, NSSM_MESSAGE_INVALID_EXIT_ACTION, action_string);
	for (int32_t i = 0; exit_action_strings[i]; i++)
		fwprintf(stderr, L"%s\n", exit_action_strings[i]);

	return -1;
}

static int32_t setting_get_exit_action(const wchar_t* service_name,[[maybe_unused]] void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	uint32_t exitcode = 0;
	uint32_t* code = 0;

	if (additional)
	{
		if (!is_default(additional))
		{
			if (str_number(additional, &exitcode))
				return -1;
			code = &exitcode;
		}
	}

	wchar_t action_string[ACTION_LEN];
	bool default_action;
	if (get_exit_action(service_name, code, action_string, &default_action))
		return -1;

	value_from_string(name, value, action_string);

	if (default_action && !_wcsnicmp((const wchar_t*)action_string, (wchar_t*)default_value, ACTION_LEN))
		return 0;
	return 1;
}

static int32_t setting_dump_exit_action(const wchar_t* service_name,[[maybe_unused]] void* param, const wchar_t* name, void* default_value, value_t* value,[[maybe_unused]] const wchar_t* additional)
{
	int32_t errors = 0;
	HKEY key = open_registry(service_name, regliterals::regexit.data(), KEY_READ);
	if (!key)
		return -1;

	wchar_t code[16];
	uint32_t index = 0;
	while (true)
	{
		int32_t ret = enumerate_registry_values(key, &index, code, std::size(code));
		if (ret == ERROR_NO_MORE_ITEMS)
			break;
		if (ret != ERROR_SUCCESS)
			continue;
		bool valid = true;
		size_t i;
		for (i = 0u; i < std::size(code); i++)
		{
			if (!code[i])
				break;
			if (code[i] >= '0' || code[i] <= '9')
				continue;
			valid = false;
			break;
		}
		if (!valid)
			continue;

		const wchar_t* additional = (code[i]) ? code : locals::defvalue.data();

		ret = setting_get_exit_action(service_name, 0, name, default_value, value, additional);
		if (ret == 1)
		{
			if (setting_dump_string(service_name, (void*)REG_SZ, name, value, additional))
				errors++;
		}
		else if (ret < 0)
			errors++;
	}

	if (errors)
		return -1;
	return 0;
}

static inline bool split_hook_name(const wchar_t* hook_name, wchar_t* hook_event, wchar_t* hook_action)
{
	wchar_t* s;

	for (s = (wchar_t*)hook_name; *s; s++)
	{
		if (*s == '/')
		{
			*s = '\0';
			::_snwprintf_s(hook_event, std::to_underlying(hookconst::namelength), _TRUNCATE, L"%s", hook_name);
			*s++ = '/';
			::_snwprintf_s(hook_action, std::to_underlying(hookconst::namelength), _TRUNCATE, L"%s", s);
			return valid_hook_name(hook_event, hook_action, false);
		}
	}

	print_message(stderr, NSSM_MESSAGE_INVALID_HOOK_NAME, hook_name);
	return false;
}

static int32_t setting_set_hook(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	wchar_t hook_event[std::to_underlying(hookconst::namelength)];
	wchar_t hook_action[std::to_underlying(hookconst::namelength)];
	if (!split_hook_name(additional, hook_event, hook_action))
		return -1;

	wchar_t* cmd;
	if (value && value->string)
		cmd = value->string;
	else
		cmd = L"";

	if (set_hook(service_name, hook_event, hook_action, cmd))
		return -1;
	if (!::wcslen(cmd))
		return 0;
	return 1;
}

static int32_t setting_get_hook(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	wchar_t hook_event[hookconst::namelength];
	wchar_t hook_action[hookconst::namelength];
	if (!split_hook_name(additional, hook_event, hook_action))
		return -1;

	wchar_t cmd[CMD_LENGTH];
	if (get_hook(service_name, hook_event, hook_action, cmd, sizeof(cmd)))
		return -1;

	value_from_string(name, value, cmd);

	if (!::wcslen(cmd))
		return 0;
	return 1;
}

static int32_t setting_dump_hooks(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	int32_t i, j;

	int32_t errors = 0;
	for (i = 0; hook_event_strings[i]; i++)
	{
		const wchar_t* hook_event = hook_event_strings[i];
		for (j = 0; hook_action_strings[j]; j++)
		{
			const wchar_t* hook_action = hook_action_strings[j];
			if (!valid_hook_name(hook_event, hook_action, true))
				continue;

			wchar_t hook_name[hookconst::namelength];
			::_snwprintf_s(hook_name, std::size(hook_name), _TRUNCATE, L"%s/%s", hook_event, hook_action);

			int32_t ret = setting_get_hook(service_name, param, name, default_value, value, hook_name);
			if (ret != 1)
			{
				if (ret < 0)
					errors++;
				continue;
			}

			if (setting_dump_string(service_name, (void*)REG_SZ, name, value, hook_name))
				errors++;
		}
	}

	if (errors)
		return -1;
	return 0;
}

static int32_t setting_set_affinity(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	HKEY key = (HKEY)param;
	if (!key)
		return -1;

	int32_t error;
	int64_t mask;
	int64_t system_affinity = 0LL;

	if (value && value->string)
	{
		uintptr_t affinity;
		if (!GetProcessAffinityMask(GetCurrentProcess(), &affinity, (uintptr_t*)&system_affinity))
			system_affinity = ~0;

		if (is_default(value->string) || str_equiv(value->string, locals::all.data()))
			mask = 0LL;
		else if (affinity_string_to_mask(value->string, &mask))
		{
			print_message(stderr, NSSM_MESSAGE_BOGUS_AFFINITY_MASK, value->string, num_cpus() - 1);
			return -1;
		}
	}
	else
		mask = 0LL;

	if (!mask)
	{
		error = ::RegDeleteValueW(key, name);
		if (error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND)
			return 0;
		print_message(stderr, NSSM_MESSAGE_REGDELETEVALUE_FAILED, name, service_name, error_string(error));
		return -1;
	}

	/* Canonicalise. */
	wchar_t* canon = 0;
	if (affinity_mask_to_string(mask, &canon))
		canon = value->string;

	int64_t effective_affinity = mask & system_affinity;
	if (effective_affinity != mask)
	{
		/* Requested CPUs did not intersect with available CPUs? */
		if (!effective_affinity)
			mask = effective_affinity = system_affinity;

		wchar_t* system = 0;
		if (!affinity_mask_to_string(system_affinity, &system))
		{
			wchar_t* effective = 0;
			if (!affinity_mask_to_string(effective_affinity, &effective))
			{
				print_message(stderr, NSSM_MESSAGE_EFFECTIVE_AFFINITY_MASK, value->string, system, effective);
				HeapFree(GetProcessHeap(), 0, effective);
			}
			HeapFree(GetProcessHeap(), 0, system);
		}
	}

	if (::RegSetValueExW(key, name, 0, REG_SZ, (const uint8_t*)canon, (uint32_t)(::wcslen(canon) + 1) * sizeof(wchar_t)) != ERROR_SUCCESS)
	{
		if (canon != value->string)
			HeapFree(GetProcessHeap(), 0, canon);
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, name, error_string(GetLastError()), 0);
		return -1;
	}

	if (canon != value->string)
		HeapFree(GetProcessHeap(), 0, canon);
	return 1;
}

static int32_t setting_get_affinity(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	HKEY key = (HKEY)param;
	if (!key)
		return -1;

	uint32_t type;
	wchar_t* buffer = 0;
	uint32_t buflen = 0;

	int32_t ret = ::RegQueryValueExW(key, name, 0, &type, 0, &buflen);
	if (ret == ERROR_FILE_NOT_FOUND)
	{
		if (value_from_string(name, value, locals::all.data()) == 1)
			return 0;
		return -1;
	}
	if (ret != ERROR_SUCCESS)
		return -1;

	if (type != REG_SZ)
		return -1;

	buffer = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, buflen);
	if (!buffer)
	{
		print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"affinity", L"setting_get_affinity");
		return -1;
	}

	if (get_string(key, (wchar_t*)name, buffer, buflen, false, false, true))
	{
		HeapFree(GetProcessHeap(), 0, buffer);
		return -1;
	}

	int64_t affinity;
	if (affinity_string_to_mask(buffer, &affinity))
	{
		print_message(stderr, NSSM_MESSAGE_BOGUS_AFFINITY_MASK, buffer, num_cpus() - 1);
		HeapFree(GetProcessHeap(), 0, buffer);
		return -1;
	}

	HeapFree(GetProcessHeap(), 0, buffer);

	/* Canonicalise. */
	if (affinity_mask_to_string(affinity, &buffer))
	{
		if (buffer)
			HeapFree(GetProcessHeap(), 0, buffer);
		return -1;
	}

	ret = value_from_string(name, value, buffer);
	HeapFree(GetProcessHeap(), 0, buffer);
	return ret;
}

static int32_t setting_set_environment(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	HKEY key = (HKEY)param;
	if (!param)
		return -1;

	wchar_t* string = 0;
	wchar_t* unformatted = 0;
	uint32_t envlen;
	uint32_t newlen = 0;
	int32_t op = 0;
	if (value && value->string && value->string[0])
	{
		string = value->string;
		switch (string[0])
		{
		case _T('+'):
			op = 1;
			break;
		case _T('-'):
			op = -1;
			break;
		case _T(':'):
			string++;
			break;
		}
	}

	if (op)
	{
		string++;
		wchar_t* env = 0;
		if (get_environment((wchar_t*)service_name, key, (wchar_t*)name, &env, &envlen))
			return -1;
		if (env)
		{
			int32_t ret;
			if (op > 0)
				ret = append_to_environment_block(env, envlen, string, &unformatted, &newlen);
			else
				ret = remove_from_environment_block(env, envlen, string, &unformatted, &newlen);
			if (envlen)
				HeapFree(GetProcessHeap(), 0, env);
			if (ret)
				return -1;

			string = unformatted;
		}
		else
		{
			/*
        No existing environment.
        We can't remove from an empty environment so just treat an add
        operation as setting a new string.
      */
			if (op < 0)
				return 0;
			op = 0;
		}
	}

	if (!string || !string[0])
	{
		int32_t error = ::RegDeleteValueW(key, name);
		if (error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND)
			return 0;
		print_message(stderr, NSSM_MESSAGE_REGDELETEVALUE_FAILED, name, service_name, error_string(error));
		return -1;
	}

	if (!op)
	{
		if (unformat_double_null(string, (uint32_t)::wcslen(string), &unformatted, &newlen))
			return -1;
	}

	if (test_environment(unformatted))
	{
		HeapFree(GetProcessHeap(), 0, unformatted);
		print_message(stderr, NSSM_GUI_INVALID_ENVIRONMENT);
		return -1;
	}

	if (::RegSetValueExW(key, name, 0, REG_MULTI_SZ, (const uint8_t*)unformatted, (uint32_t)newlen * sizeof(wchar_t)) != ERROR_SUCCESS)
	{
		if (newlen)
			HeapFree(GetProcessHeap(), 0, unformatted);
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, regliterals::regenv.data(), error_string(GetLastError()), 0);
		return -1;
	}

	if (newlen)
		HeapFree(GetProcessHeap(), 0, unformatted);
	return 1;
}

static int32_t setting_get_environment(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	HKEY key = (HKEY)param;
	if (!param)
		return -1;

	wchar_t* env = 0;
	uint32_t envlen;
	if (get_environment((wchar_t*)service_name, key, (wchar_t*)name, &env, &envlen))
		return -1;
	if (!envlen)
		return 0;

	wchar_t* formatted;
	uint32_t newlen;
	if (format_double_null(env, envlen, &formatted, &newlen))
		return -1;

	int32_t ret;
	if (additional)
	{
		/* Find named environment variable. */
		wchar_t* s;
		size_t len = ::wcslen(additional);
		for (s = env; *s; s++)
		{
			/* Look for <additional>=<string> nullptr nullptr */
			if (!_wcsnicmp(s, additional, len) && s[len] == _T('='))
			{
				/* Strip <key>= */
				s += len + 1;
				ret = value_from_string(name, value, s);
				HeapFree(GetProcessHeap(), 0, env);
				return ret;
			}

			/* Skip this string. */
			for (; *s; s++)
				;
		}
		HeapFree(GetProcessHeap(), 0, env);
		return 0;
	}

	HeapFree(GetProcessHeap(), 0, env);

	ret = value_from_string(name, value, formatted);
	if (newlen)
		HeapFree(GetProcessHeap(), 0, formatted);
	return ret;
}

static int32_t setting_dump_environment(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	int32_t errors = 0;
	HKEY key = (HKEY)param;
	if (!param)
		return -1;

	wchar_t* env = 0;
	uint32_t envlen;
	if (get_environment((wchar_t*)service_name, key, (wchar_t*)name, &env, &envlen))
		return -1;
	if (!envlen)
		return 0;

	wchar_t* s;
	for (s = env; *s; s++)
	{
		size_t len = ::wcslen(s) + 2;
		value->string = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, len * sizeof(wchar_t));
		if (!value->string)
		{
			print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"dump", L"setting_dump_environment");
			break;
		}

		::_snwprintf_s(value->string, len, _TRUNCATE, L"%c%s", (s > env) ? _T('+') : _T(':'), s);
		if (setting_dump_string(service_name, (void*)REG_SZ, name, value, 0))
			errors++;
		HeapFree(GetProcessHeap(), 0, value->string);
		value->string = 0;

		for (; *s; s++)
			;
	}

	HeapFree(GetProcessHeap(), 0, env);

	if (errors)
		return 1;
	return 0;
}

static int32_t setting_set_priority(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	HKEY key = (HKEY)param;
	if (!param)
		return -1;

	wchar_t* priority_string;
	int32_t i;
	int32_t error;

	if (value && value->string)
		priority_string = value->string;
	else if (default_value)
		priority_string = (wchar_t*)default_value;
	else
	{
		error = ::RegDeleteValueW(key, name);
		if (error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND)
			return 0;
		print_message(stderr, NSSM_MESSAGE_REGDELETEVALUE_FAILED, name, service_name, error_string(error));
		return -1;
	}

	for (i = 0; priority_strings[i]; i++)
	{
		if (!str_equiv(priority_strings[i], priority_string))
			continue;

		if (default_value && str_equiv(priority_string, (wchar_t*)default_value))
		{
			error = ::RegDeleteValueW(key, name);
			if (error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND)
				return 0;
			print_message(stderr, NSSM_MESSAGE_REGDELETEVALUE_FAILED, name, service_name, error_string(error));
			return -1;
		}

		if (set_number(key, (wchar_t*)name, priority_index_to_constant(i)))
			return -1;
		return 1;
	}

	print_message(stderr, NSSM_MESSAGE_INVALID_PRIORITY, priority_string);
	for (i = 0; priority_strings[i]; i++)
		fwprintf(stderr, L"%s\n", priority_strings[i]);

	return -1;
}

static int32_t setting_get_priority(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	HKEY key = (HKEY)param;
	if (!param)
		return -1;

	uint32_t constant;
	switch (get_number(key, (wchar_t*)name, &constant, false))
	{
	case 0:
		if (value_from_string(name, value, (const wchar_t*)default_value) == -1)
			return -1;
		return 0;
	case -1:
		return -1;
	}

	return value_from_string(name, value, priority_strings[priority_constant_to_index(constant)]);
}

static int32_t setting_dump_priority(const wchar_t* service_name, void* key_ptr, const wchar_t* name, void* setting_ptr, value_t* value, const wchar_t* additional)
{
	settings_t* setting = (settings_t*)setting_ptr;
	int32_t ret = setting_get_priority(service_name, key_ptr, name, (void*)setting->default_value, value, 0);
	if (ret != 1)
		return ret;
	return setting_dump_string(service_name, (void*)REG_SZ, name, value, 0);
}

/* Functions to manage native service settings. */
static int32_t native_set_dependon(const wchar_t* service_name, SC_HANDLE service_handle, wchar_t** dependencies, uint32_t* dependencieslen, value_t* value, int32_t type)
{
	*dependencieslen = 0;
	if (!value || !value->string || !value->string[0])
		return 0;

	wchar_t* string = value->string;
	uint32_t buflen;
	int32_t op = 0;
	switch (string[0])
	{
	case _T('+'):
		op = 1;
		break;
	case _T('-'):
		op = -1;
		break;
	case _T(':'):
		string++;
		break;
	}

	if (op)
	{
		string++;
		wchar_t* buffer = 0;
		if (get_service_dependencies(service_name, service_handle, &buffer, &buflen, type))
			return -1;
		if (buffer)
		{
			int32_t ret;
			if (op > 0)
				ret = append_to_dependencies(buffer, buflen, string, dependencies, dependencieslen, type);
			else
				ret = remove_from_dependencies(buffer, buflen, string, dependencies, dependencieslen, type);
			if (buflen)
				HeapFree(GetProcessHeap(), 0, buffer);
			return ret;
		}
		else
		{
			/*
        No existing list.
        We can't remove from an empty list so just treat an add
        operation as setting a new string.
      */
			if (op < 0)
				return 0;
			op = 0;
		}
	}

	if (!op)
	{
		wchar_t* unformatted = 0;
		uint32_t newlen;
		if (unformat_double_null(string, (uint32_t)::wcslen(string), &unformatted, &newlen))
			return -1;

		if (type == dependency::groups)
		{
			/* Prepend group identifier. */
			uint32_t missing = 0;
			wchar_t* canon = unformatted;
			size_t canonlen = 0;
			wchar_t* s;
			for (s = unformatted; *s; s++)
			{
				if (*s != SC_GROUP_IDENTIFIER)
					missing++;
				size_t len = ::wcslen(s);
				canonlen += len + 1;
				s += len;
			}

			if (missing)
			{
				/* Missing identifiers plus double nullptr terminator. */
				canonlen += missing + 1;

				canon = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, canonlen * sizeof(wchar_t));
				if (!canon)
				{
					print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"canon", L"native_set_dependon");
					if (unformatted)
						HeapFree(GetProcessHeap(), 0, unformatted);
					return -1;
				}

				size_t i = 0;
				for (s = unformatted; *s; s++)
				{
					if (*s != SC_GROUP_IDENTIFIER)
						canon[i++] = SC_GROUP_IDENTIFIER;
					size_t len = ::wcslen(s);
					memmove(canon + i, s, (len + 1) * sizeof(wchar_t));
					i += len + 1;
					s += len;
				}

				HeapFree(GetProcessHeap(), 0, unformatted);
				unformatted = canon;
				newlen = (uint32_t)canonlen;
			}
		}

		*dependencies = unformatted;
		*dependencieslen = newlen;
	}

	return 0;
}

static int32_t native_set_dependongroup(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	SC_HANDLE service_handle = (SC_HANDLE)param;
	if (!service_handle)
		return -1;

	/*
    Get existing service dependencies because we must set both types together.
  */
	wchar_t* services_buffer;
	uint32_t services_buflen;
	if (get_service_dependencies(service_name, service_handle, &services_buffer, &services_buflen, dependency::services))
		return -1;

	if (!value || !value->string || !value->string[0])
	{
		if (!ChangeServiceConfig(service_handle, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, 0, 0, 0, services_buffer, 0, 0, 0))
		{
			print_message(stderr, NSSM_MESSAGE_CHANGESERVICECONFIG_FAILED, error_string(GetLastError()));
			if (services_buffer)
				HeapFree(GetProcessHeap(), 0, services_buffer);
			return -1;
		}

		if (services_buffer)
			HeapFree(GetProcessHeap(), 0, services_buffer);
		return 0;
	}

	/* Update the group list. */
	wchar_t* groups_buffer;
	uint32_t groups_buflen;
	if (native_set_dependon(service_name, service_handle, &groups_buffer, &groups_buflen, value, dependency::groups))
		return -1;

	wchar_t* dependencies;
	if (services_buflen > 2)
	{
		dependencies = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (groups_buflen + services_buflen) * sizeof(wchar_t));
		if (!dependencies)
		{
			print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"dependencies", L"native_set_dependongroup");
			if (groups_buffer)
				HeapFree(GetProcessHeap(), 0, groups_buffer);
			if (services_buffer)
				HeapFree(GetProcessHeap(), 0, services_buffer);
			return -1;
		}

		memmove(dependencies, services_buffer, services_buflen * sizeof(wchar_t));
		memmove(dependencies + services_buflen - 1, groups_buffer, groups_buflen * sizeof(wchar_t));
	}
	else
		dependencies = groups_buffer;

	int32_t ret = 1;
	if (set_service_dependencies(service_name, service_handle, dependencies))
		ret = -1;
	if (dependencies != groups_buffer)
		HeapFree(GetProcessHeap(), 0, dependencies);
	if (groups_buffer)
		HeapFree(GetProcessHeap(), 0, groups_buffer);
	if (services_buffer)
		HeapFree(GetProcessHeap(), 0, services_buffer);

	return ret;
}

static int32_t native_get_dependongroup(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	SC_HANDLE service_handle = (SC_HANDLE)param;
	if (!service_handle)
		return -1;

	wchar_t* buffer;
	uint32_t buflen;
	if (get_service_dependencies(service_name, service_handle, &buffer, &buflen, dependency::groups))
		return -1;

	int32_t ret;
	if (buflen)
	{
		wchar_t* formatted;
		uint32_t newlen;
		if (format_double_null(buffer, buflen, &formatted, &newlen))
		{
			HeapFree(GetProcessHeap(), 0, buffer);
			return -1;
		}

		ret = value_from_string(name, value, formatted);
		HeapFree(GetProcessHeap(), 0, formatted);
		HeapFree(GetProcessHeap(), 0, buffer);
	}
	else
	{
		value->string = 0;
		ret = 0;
	}

	return ret;
}

static int32_t setting_dump_dependon(const wchar_t* service_name, SC_HANDLE service_handle, const wchar_t* name, int32_t type, value_t* value)
{
	int32_t errors = 0;

	wchar_t* dependencies = 0;
	uint32_t dependencieslen;
	if (get_service_dependencies(service_name, service_handle, &dependencies, &dependencieslen, type))
		return -1;
	if (!dependencieslen)
		return 0;

	wchar_t* s;
	for (s = dependencies; *s; s++)
	{
		size_t len = ::wcslen(s) + 2;
		value->string = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, len * sizeof(wchar_t));
		if (!value->string)
		{
			print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"dump", L"setting_dump_dependon");
			break;
		}

		::_snwprintf_s(value->string, len, _TRUNCATE, L"%c%s", (s > dependencies) ? _T('+') : _T(':'), s);
		if (setting_dump_string(service_name, (void*)REG_SZ, name, value, 0))
			errors++;
		HeapFree(GetProcessHeap(), 0, value->string);
		value->string = 0;

		for (; *s; s++)
			;
	}

	HeapFree(GetProcessHeap(), 0, dependencies);

	if (errors)
		return 1;
	return 0;
}

static int32_t native_dump_dependongroup(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	return setting_dump_dependon(service_name, (SC_HANDLE)param, name, dependency::groups, value);
}

static int32_t native_set_dependonservice(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	SC_HANDLE service_handle = (SC_HANDLE)param;
	if (!service_handle)
		return -1;

	/*
    Get existing group dependencies because we must set both types together.
  */
	wchar_t* groups_buffer;
	uint32_t groups_buflen;
	if (get_service_dependencies(service_name, service_handle, &groups_buffer, &groups_buflen, dependency::groups))
		return -1;

	if (!value || !value->string || !value->string[0])
	{
		if (!ChangeServiceConfig(service_handle, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, 0, 0, 0, groups_buffer, 0, 0, 0))
		{
			print_message(stderr, NSSM_MESSAGE_CHANGESERVICECONFIG_FAILED, error_string(GetLastError()));
			if (groups_buffer)
				HeapFree(GetProcessHeap(), 0, groups_buffer);
			return -1;
		}

		if (groups_buffer)
			HeapFree(GetProcessHeap(), 0, groups_buffer);
		return 0;
	}

	/* Update the service list. */
	wchar_t* services_buffer;
	uint32_t services_buflen;
	if (native_set_dependon(service_name, service_handle, &services_buffer, &services_buflen, value, dependency::services))
		return -1;

	wchar_t* dependencies;
	if (groups_buflen > 2)
	{
		dependencies = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (services_buflen + groups_buflen) * sizeof(wchar_t));
		if (!dependencies)
		{
			print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"dependencies", L"native_set_dependonservice");
			if (groups_buffer)
				HeapFree(GetProcessHeap(), 0, groups_buffer);
			if (services_buffer)
				HeapFree(GetProcessHeap(), 0, services_buffer);
			return -1;
		}

		memmove(dependencies, services_buffer, services_buflen * sizeof(wchar_t));
		memmove(dependencies + services_buflen - 1, groups_buffer, groups_buflen * sizeof(wchar_t));
	}
	else
		dependencies = services_buffer;

	int32_t ret = 1;
	if (set_service_dependencies(service_name, service_handle, dependencies))
		ret = -1;
	if (dependencies != services_buffer)
		HeapFree(GetProcessHeap(), 0, dependencies);
	if (groups_buffer)
		HeapFree(GetProcessHeap(), 0, groups_buffer);
	if (services_buffer)
		HeapFree(GetProcessHeap(), 0, services_buffer);

	return ret;
}

static int32_t native_get_dependonservice(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	SC_HANDLE service_handle = (SC_HANDLE)param;
	if (!service_handle)
		return -1;

	wchar_t* buffer;
	uint32_t buflen;
	if (get_service_dependencies(service_name, service_handle, &buffer, &buflen, dependency::services))
		return -1;

	int32_t ret;
	if (buflen)
	{
		wchar_t* formatted;
		uint32_t newlen;
		if (format_double_null(buffer, buflen, &formatted, &newlen))
		{
			HeapFree(GetProcessHeap(), 0, buffer);
			return -1;
		}

		ret = value_from_string(name, value, formatted);
		HeapFree(GetProcessHeap(), 0, formatted);
		HeapFree(GetProcessHeap(), 0, buffer);
	}
	else
	{
		value->string = 0;
		ret = 0;
	}

	return ret;
}

static int32_t native_dump_dependonservice(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	return setting_dump_dependon(service_name, (SC_HANDLE)param, name, dependency::services, value);
}

int32_t native_set_description(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	SC_HANDLE service_handle = (SC_HANDLE)param;
	if (!service_handle)
		return -1;

	wchar_t* description = 0;
	if (value)
		description = value->string;
	if (set_service_description(service_name, service_handle, description))
		return -1;

	if (description && description[0])
		return 1;

	return 0;
}

int32_t native_get_description(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	SC_HANDLE service_handle = (SC_HANDLE)param;
	if (!service_handle)
		return -1;

	wchar_t buffer[VALUE_LENGTH];
	if (get_service_description(service_name, service_handle, std::size(buffer), buffer))
		return -1;

	if (buffer[0])
		return value_from_string(name, value, buffer);
	value->string = 0;

	return 0;
}

int32_t native_set_displayname(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	SC_HANDLE service_handle = (SC_HANDLE)param;
	if (!service_handle)
		return -1;

	wchar_t* displayname = 0;
	if (value && value->string)
		displayname = value->string;
	else
		displayname = (wchar_t*)service_name;

	if (!ChangeServiceConfig(service_handle, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, 0, 0, 0, 0, 0, 0, displayname))
	{
		print_message(stderr, NSSM_MESSAGE_CHANGESERVICECONFIG_FAILED, error_string(GetLastError()));
		return -1;
	}

	/*
    If the display name and service name differ only in case,
    ChangeServiceConfig() will return success but the display name will be
    set to the service name, NOT the value passed to the function.
    This appears to be a quirk of Windows rather than a bug here.
  */
	if (displayname != service_name && !str_equiv(displayname, service_name))
		return 1;

	return 0;
}

int32_t native_get_displayname(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	SC_HANDLE service_handle = (SC_HANDLE)param;
	if (!service_handle)
		return -1;

	QUERY_SERVICE_CONFIGW* qsc = query_service_config(service_name, service_handle);
	if (!qsc)
		return -1;

	int32_t ret = value_from_string(name, value, qsc->lpDisplayName);
	HeapFree(GetProcessHeap(), 0, qsc);

	return ret;
}

int32_t native_set_environment(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	HKEY key = open_service_registry(service_name, KEY_SET_VALUE, true);
	if (!key)
		return -1;

	int32_t ret = setting_set_environment(service_name, (void*)key, name, default_value, value, additional);
	RegCloseKey(key);
	return ret;
}

int32_t native_get_environment(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	HKEY key = open_service_registry(service_name, KEY_READ, true);
	if (!key)
		return -1;

	ZeroMemory(value, sizeof(value_t));
	int32_t ret = setting_get_environment(service_name, (void*)key, name, default_value, value, additional);
	RegCloseKey(key);
	return ret;
}

static int32_t native_dump_environment(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	HKEY key = open_service_registry(service_name, KEY_READ, true);
	if (!key)
		return -1;

	int32_t ret = setting_dump_environment(service_name, (void*)key, name, default_value, value, additional);
	RegCloseKey(key);
	return ret;
}

int32_t native_set_imagepath(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	SC_HANDLE service_handle = (SC_HANDLE)param;
	if (!service_handle)
		return -1;

	/* It makes no sense to try to reset the image path. */
	if (!value || !value->string)
	{
		print_message(stderr, NSSM_MESSAGE_NO_DEFAULT_VALUE, name);
		return -1;
	}

	if (!ChangeServiceConfig(service_handle, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, value->string, 0, 0, 0, 0, 0, 0))
	{
		print_message(stderr, NSSM_MESSAGE_CHANGESERVICECONFIG_FAILED, error_string(GetLastError()));
		return -1;
	}

	return 1;
}

int32_t native_get_imagepath(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	SC_HANDLE service_handle = (SC_HANDLE)param;
	if (!service_handle)
		return -1;

	QUERY_SERVICE_CONFIGW* qsc = query_service_config(service_name, service_handle);
	if (!qsc)
		return -1;

	int32_t ret = value_from_string(name, value, qsc->lpBinaryPathName);
	HeapFree(GetProcessHeap(), 0, qsc);

	return ret;
}

int32_t native_set_name(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	print_message(stderr, NSSM_MESSAGE_CANNOT_RENAME_SERVICE);
	return -1;
}

int32_t native_get_name(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	return value_from_string(name, value, service_name);
}

int32_t native_set_objectname(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	SC_HANDLE service_handle = (SC_HANDLE)param;
	if (!service_handle)
		return -1;

	/*
    Logical syntax is: nssm set <service> ObjectName <username> <password>
    That means the username is actually passed in the additional parameter.
  */
	bool localsystem = false;
	bool virtual_account = false;
	wchar_t* username = NSSM_LOCALSYSTEM_ACCOUNT;
	wchar_t* password = 0;
	if (additional)
	{
		username = (wchar_t*)additional;
		if (value && value->string)
			password = value->string;
	}
	else if (value && value->string)
		username = value->string;

	const wchar_t* well_known = well_known_username(username);
	size_t passwordsize = 0;
	if (well_known)
	{
		if (str_equiv(well_known, NSSM_LOCALSYSTEM_ACCOUNT))
			localsystem = true;
		username = (wchar_t*)well_known;
		password = L"";
	}
	else if (is_virtual_account(service_name, username))
		virtual_account = true;
	else if (!password)
	{
		/* We need a password if the account requires it. */
		print_message(stderr, NSSM_MESSAGE_MISSING_PASSWORD, name);
		return -1;
	}
	else
		passwordsize = ::wcslen(password) * sizeof(wchar_t);

	/*
    ChangeServiceConfig() will fail to set the username if the service is set
    to interact with the desktop.
  */
	uint32_t type = SERVICE_NO_CHANGE;
	if (!localsystem)
	{
		QUERY_SERVICE_CONFIGW* qsc = query_service_config(service_name, service_handle);
		if (!qsc)
		{
			if (passwordsize)
				SecureZeroMemory(password, passwordsize);
			return -1;
		}

		type = qsc->dwServiceType & ~SERVICE_INTERACTIVE_PROCESS;
		HeapFree(GetProcessHeap(), 0, qsc);
	}

	if (!well_known && !virtual_account)
	{
		if (grant_logon_as_service(username))
		{
			if (passwordsize)
				SecureZeroMemory(password, passwordsize);
			print_message(stderr, NSSM_MESSAGE_GRANT_LOGON_AS_SERVICE_FAILED, username);
			return -1;
		}
	}

	if (!ChangeServiceConfig(service_handle, type, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, 0, 0, 0, 0, username, password, 0))
	{
		if (passwordsize)
			SecureZeroMemory(password, passwordsize);
		print_message(stderr, NSSM_MESSAGE_CHANGESERVICECONFIG_FAILED, error_string(GetLastError()));
		return -1;
	}

	if (passwordsize)
		SecureZeroMemory(password, passwordsize);

	if (localsystem)
		return 0;

	return 1;
}

int32_t native_get_objectname(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	SC_HANDLE service_handle = (SC_HANDLE)param;
	if (!service_handle)
		return -1;

	QUERY_SERVICE_CONFIGW* qsc = query_service_config(service_name, service_handle);
	if (!qsc)
		return -1;

	int32_t ret = value_from_string(name, value, qsc->lpServiceStartName);
	HeapFree(GetProcessHeap(), 0, qsc);

	return ret;
}

int32_t native_dump_objectname(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	int32_t ret = native_get_objectname(service_name, param, name, default_value, value, additional);
	if (ret != 1)
		return ret;

	/* Properly checking for a virtual account requires the actual service name. */
	if (!_wcsnicmp(NSSM_VIRTUAL_SERVICE_ACCOUNT_DOMAIN, value->string, ::wcslen(NSSM_VIRTUAL_SERVICE_ACCOUNT_DOMAIN)))
	{
		wchar_t* name = virtual_account(service_name);
		if (!name)
			return -1;
		HeapFree(GetProcessHeap(), 0, value->string);
		value->string = name;
	}
	else
	{
		/* Do we need to dump a dummy password? */
		if (!well_known_username(value->string))
		{
			/* Parameters are the other way round. */
			value_t inverted;
			inverted.string = L"****";
			return setting_dump_string(service_name, (void*)REG_SZ, name, &inverted, value->string);
		}
	}
	return setting_dump_string(service_name, (void*)REG_SZ, name, value, 0);
}

int32_t native_set_startup(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	SC_HANDLE service_handle = (SC_HANDLE)param;
	if (!service_handle)
		return -1;

	/* It makes no sense to try to reset the startup type. */
	if (!value || !value->string)
	{
		print_message(stderr, NSSM_MESSAGE_NO_DEFAULT_VALUE, name);
		return -1;
	}

	/* Map NSSM_STARTUP_* constant to Windows SERVICE_*_START constant. */
	int32_t service_startup = -1;
	int32_t i;
	for (i = 0; startup_strings[i]; i++)
	{
		if (str_equiv(value->string, startup_strings[i]))
		{
			service_startup = i;
			break;
		}
	}

	if (service_startup < 0)
	{
		print_message(stderr, NSSM_MESSAGE_INVALID_SERVICE_STARTUP, value->string);
		for (i = 0; startup_strings[i]; i++)
			fwprintf(stderr, L"%s\n", startup_strings[i]);
		return -1;
	}

	uint32_t startup;
	switch (service_startup)
	{
	case startup::manual:
		startup = SERVICE_DEMAND_START;
		break;
	case startup::disabled:
		startup = SERVICE_DISABLED;
		break;
	default:
		startup = SERVICE_AUTO_START;
	}

	if (!ChangeServiceConfig(service_handle, SERVICE_NO_CHANGE, startup, SERVICE_NO_CHANGE, 0, 0, 0, 0, 0, 0, 0))
	{
		print_message(stderr, NSSM_MESSAGE_CHANGESERVICECONFIG_FAILED, error_string(GetLastError()));
		return -1;
	}

	SERVICE_DELAYED_AUTO_START_INFO delayed;
	ZeroMemory(&delayed, sizeof(delayed));
	if (service_startup == startup::delayed)
		delayed.fDelayedAutostart = 1;
	else
		delayed.fDelayedAutostart = 0;
	if (!ChangeServiceConfig2(service_handle, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &delayed))
	{
		uint32_t error = GetLastError();
		/* Pre-Vista we expect to fail with ERROR_INVALID_LEVEL */
		if (error != ERROR_INVALID_LEVEL)
		{
			log_event(EVENTLOG_ERROR_TYPE, NSSM_MESSAGE_SERVICE_CONFIG_DELAYED_AUTO_START_INFO_FAILED, service_name, error_string(error), 0);
		}
	}

	return 1;
}

int32_t native_get_startup(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	SC_HANDLE service_handle = (SC_HANDLE)param;
	if (!service_handle)
		return -1;

	QUERY_SERVICE_CONFIGW* qsc = query_service_config(service_name, service_handle);
	if (!qsc)
		return -1;

	uint32_t startup;
	int32_t ret = get_service_startup(service_name, service_handle, qsc, &startup);
	HeapFree(GetProcessHeap(), 0, qsc);

	if (ret)
		return -1;

	uint32_t i;
	for (i = 0; startup_strings[i]; i++)
		;
	if (startup >= i)
		return -1;

	return value_from_string(name, value, startup_strings[startup]);
}

int32_t native_set_type(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	SC_HANDLE service_handle = (SC_HANDLE)param;
	if (!service_handle)
		return -1;

	/* It makes no sense to try to reset the service type. */
	if (!value || !value->string)
	{
		print_message(stderr, NSSM_MESSAGE_NO_DEFAULT_VALUE, name);
		return -1;
	}

	/*
    We can only manage services of type SERVICE_WIN32_OWN_PROCESS
    and SERVICE_INTERACTIVE_PROCESS.
  */
	uint32_t type = SERVICE_WIN32_OWN_PROCESS;
	if (str_equiv(value->string, NSSM_INTERACTIVE_PROCESS))
		type |= SERVICE_INTERACTIVE_PROCESS;
	else if (!str_equiv(value->string, NSSM_WIN32_OWN_PROCESS))
	{
		print_message(stderr, NSSM_MESSAGE_INVALID_SERVICE_TYPE, value->string);
		fwprintf(stderr, L"%s\n", NSSM_WIN32_OWN_PROCESS);
		fwprintf(stderr, L"%s\n", NSSM_INTERACTIVE_PROCESS);
		return -1;
	}

	/*
    ChangeServiceConfig() will fail if the service runs under an account
    other than LOCALSYSTEM and we try to make it interactive.
  */
	if (type & SERVICE_INTERACTIVE_PROCESS)
	{
		QUERY_SERVICE_CONFIGW* qsc = query_service_config(service_name, service_handle);
		if (!qsc)
			return -1;

		if (!str_equiv(qsc->lpServiceStartName, NSSM_LOCALSYSTEM_ACCOUNT))
		{
			HeapFree(GetProcessHeap(), 0, qsc);
			print_message(stderr, NSSM_MESSAGE_INTERACTIVE_NOT_LOCALSYSTEM, value->string, service_name, NSSM_LOCALSYSTEM_ACCOUNT);
			return -1;
		}

		HeapFree(GetProcessHeap(), 0, qsc);
	}

	if (!ChangeServiceConfig(service_handle, type, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, 0, 0, 0, 0, 0, 0, 0))
	{
		print_message(stderr, NSSM_MESSAGE_CHANGESERVICECONFIG_FAILED, error_string(GetLastError()));
		return -1;
	}

	return 1;
}

int32_t native_get_type(const wchar_t* service_name, void* param, const wchar_t* name, void* default_value, value_t* value, const wchar_t* additional)
{
	SC_HANDLE service_handle = (SC_HANDLE)param;
	if (!service_handle)
		return -1;

	QUERY_SERVICE_CONFIGW* qsc = query_service_config(service_name, service_handle);
	if (!qsc)
		return -1;

	value->numeric = qsc->dwServiceType;
	HeapFree(GetProcessHeap(), 0, qsc);

	const wchar_t* string;
	switch (value->numeric)
	{
	case SERVICE_KERNEL_DRIVER:
		string = NSSM_KERNEL_DRIVER;
		break;
	case SERVICE_FILE_SYSTEM_DRIVER:
		string = NSSM_FILE_SYSTEM_DRIVER;
		break;
	case SERVICE_WIN32_OWN_PROCESS:
		string = NSSM_WIN32_OWN_PROCESS;
		break;
	case SERVICE_WIN32_SHARE_PROCESS:
		string = NSSM_WIN32_SHARE_PROCESS;
		break;
	case SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS:
		string = NSSM_INTERACTIVE_PROCESS;
		break;
	case SERVICE_WIN32_SHARE_PROCESS | SERVICE_INTERACTIVE_PROCESS:
		string = NSSM_SHARE_INTERACTIVE_PROCESS;
		break;
	default:
		string = NSSM_UNKNOWN;
	}

	return value_from_string(name, value, string);
}

int32_t set_setting(const wchar_t* service_name, HKEY key, settings_t* setting, value_t* value, const wchar_t* additional)
{
	if (!key)
		return -1;
	int32_t ret;

	if (setting->set)
		ret = setting->set(service_name, (void*)key, setting->name, setting->default_value, value, additional);
	else
		ret = -1;

	if (!ret)
		print_message(stdout, NSSM_MESSAGE_RESET_SETTING, setting->name, service_name);
	else if (ret > 0)
		print_message(stdout, NSSM_MESSAGE_SET_SETTING, setting->name, service_name);
	else
		print_message(stderr, NSSM_MESSAGE_SET_SETTING_FAILED, setting->name, service_name);

	return ret;
}

int32_t set_setting(const wchar_t* service_name, SC_HANDLE service_handle, settings_t* setting, value_t* value, const wchar_t* additional)
{
	if (!service_handle)
		return -1;

	int32_t ret;
	if (setting->set)
		ret = setting->set(service_name, service_handle, setting->name, setting->default_value, value, additional);
	else
		ret = -1;

	if (!ret)
		print_message(stdout, NSSM_MESSAGE_RESET_SETTING, setting->name, service_name);
	else if (ret > 0)
		print_message(stdout, NSSM_MESSAGE_SET_SETTING, setting->name, service_name);
	else
		print_message(stderr, NSSM_MESSAGE_SET_SETTING_FAILED, setting->name, service_name);

	return ret;
}

/*
  Returns:  1 if the value was retrieved.
            0 if the default value was retrieved.
           -1 on error.
*/
int32_t get_setting(const wchar_t* service_name, HKEY key, settings_t* setting, value_t* value, const wchar_t* additional)
{
	if (!key)
		return -1;
	int32_t ret;

	if (is_string_type(setting->type))
	{
		value->string = (wchar_t*)setting->default_value;
		if (setting->get)
			ret = setting->get(service_name, (void*)key, setting->name, setting->default_value, value, additional);
		else
			ret = -1;
	}
	else if (is_numeric_type(setting->type))
	{
		value->numeric = PtrToUlong(setting->default_value);
		if (setting->get)
			ret = setting->get(service_name, (void*)key, setting->name, setting->default_value, value, additional);
		else
			ret = -1;
	}
	else
		ret = -1;

	if (ret < 0)
		print_message(stderr, NSSM_MESSAGE_GET_SETTING_FAILED, setting->name, service_name);

	return ret;
}

int32_t get_setting(const wchar_t* service_name, SC_HANDLE service_handle, settings_t* setting, value_t* value, const wchar_t* additional)
{
	if (!service_handle)
		return -1;
	return setting->get(service_name, service_handle, setting->name, 0, value, additional);
}

int32_t dump_setting(const wchar_t* service_name, HKEY key, SC_HANDLE service_handle, settings_t* setting)
{
	void* param;
	if (setting->native)
	{
		if (!service_handle)
			return -1;
		param = (void*)service_handle;
	}
	else
	{
		/* Will be null for native services. */
		param = (void*)key;
	}

	value_t value = {0};
	int32_t ret;

	if (setting->dump)
		return setting->dump(service_name, param, setting->name, (void*)setting, &value, 0);
	if (setting->native)
		ret = get_setting(service_name, service_handle, setting, &value, 0);
	else
		ret = get_setting(service_name, key, setting, &value, 0);
	if (ret != 1)
		return ret;
	return setting_dump_string(service_name, (void*)setting->type, setting->name, &value, 0);
}

settings_t settings[] = {
	{regliterals::regexe.data(), REG_EXPAND_SZ, (void*)L"", false, 0, setting_set_string, setting_get_string, setting_not_dumpable},
	{regliterals::regflags.data(), REG_EXPAND_SZ, (void*)L"", false, 0, setting_set_string, setting_get_string, 0},
	{regliterals::regdir.data(), REG_EXPAND_SZ, (void*)L"", false, 0, setting_set_string, setting_get_string, 0},
	{regliterals::regexit.data(), REG_SZ, (void*)exit_action_strings[exit::restart], false, additionalarg::mandatory, setting_set_exit_action, setting_get_exit_action, setting_dump_exit_action},
	{regliterals::reghook, REG_SZ, (void*)L"", false, additionalarg::mandatory, setting_set_hook, setting_get_hook, setting_dump_hooks},
	{regliterals::regaffinity, REG_SZ, 0, false, 0, setting_set_affinity, setting_get_affinity, 0},
	{regliterals::regenv.data(), REG_MULTI_SZ, nullptr, false, additionalarg::crlf, setting_set_environment, setting_get_environment, setting_dump_environment},
	{regliterals::regenvextra.data(), REG_MULTI_SZ, nullptr, false, additionalarg::crlf, setting_set_environment, setting_get_environment, setting_dump_environment},
	{regliterals::regnoconsole, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regpriority, REG_SZ, (void*)priority_strings[priority::normal], false, 0, setting_set_priority, setting_get_priority, setting_dump_priority},
	{regliterals::regrestartdelay, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regstdin, REG_EXPAND_SZ, nullptr, false, 0, setting_set_string, setting_get_string, 0},
	{regliterals::regstdin regliterals::regsharemode, REG_DWORD, (void*)NSSM_STDIN_SHARING, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regstdin regliterals::regdisposition, REG_DWORD, (void*)NSSM_STDIN_DISPOSITION, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regstdin regliterals::regstdioflags, REG_DWORD, (void*)NSSM_STDIN_FLAGS, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regstdout, REG_EXPAND_SZ, nullptr, false, 0, setting_set_string, setting_get_string, 0},
	{regliterals::regstdout regliterals::regsharemode, REG_DWORD, (void*)NSSM_STDOUT_SHARING, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regstdout regliterals::regdisposition, REG_DWORD, (void*)NSSM_STDOUT_DISPOSITION, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regstdout regliterals::regstdioflags, REG_DWORD, (void*)NSSM_STDOUT_FLAGS, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regstdout regliterals::regcopytruncate, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regstderr, REG_EXPAND_SZ, nullptr, false, 0, setting_set_string, setting_get_string, 0},
	{regliterals::regstderr regliterals::regsharemode, REG_DWORD, (void*)NSSM_STDERR_SHARING, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regstderr regliterals::regdisposition, REG_DWORD, (void*)NSSM_STDERR_DISPOSITION, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regstderr regliterals::regstdioflags, REG_DWORD, (void*)NSSM_STDERR_FLAGS, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regstderr regliterals::regcopytruncate, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regstopmethodskip, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regkillconsolegraceperiod, REG_DWORD, (void*)wait::kill_console_grace_period, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regkillwindowgraceperiod, REG_DWORD, (void*)wait::kill_window_grace_period, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regkillthreadsgraceperiod, REG_DWORD, (void*)wait::kill_threads_grace_period, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regkillprocesstree, REG_DWORD, (void*)1, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regthrottle, REG_DWORD, (void*)wait::reset_throttle_restart, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regredirecthook, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regrotate, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regrotateonline, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regrotateseconds, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regrotatebyteslow, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regrotatebyteshigh, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regrotatedelay, REG_DWORD, (void*)wait::rotatedelay, false, 0, setting_set_number, setting_get_number, 0},
	{regliterals::regtimestamplog, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0},
	{nativeliterals::dependongroup.data(), REG_MULTI_SZ, nullptr, true, additionalarg::crlf, native_set_dependongroup, native_get_dependongroup, native_dump_dependongroup},
	{nativeliterals::dependonservice.data(), REG_MULTI_SZ, nullptr, true, additionalarg::crlf, native_set_dependonservice, native_get_dependonservice, native_dump_dependonservice},
	{nativeliterals::description.data(), REG_SZ, L"", true, 0, native_set_description, native_get_description, 0},
	{nativeliterals::displaynam.data(), REG_SZ, nullptr, true, 0, native_set_displayname, native_get_displayname, 0},
	{nativeliterals::environment.data(), REG_MULTI_SZ, nullptr, true, additionalarg::crlf, native_set_environment, native_get_environment, native_dump_environment},
	{nativeliterals::imagepath.data(), REG_EXPAND_SZ, nullptr, true, 0, native_set_imagepath, native_get_imagepath, setting_not_dumpable},
	{nativeliterals::objectname.data(), REG_SZ, NSSM_LOCALSYSTEM_ACCOUNT, true, 0, native_set_objectname, native_get_objectname, native_dump_objectname},
	{nativeliterals::name.data(), REG_SZ, nullptr, true, 0, native_set_name, native_get_name, setting_not_dumpable},
	{nativeliterals::start.data(), REG_SZ, nullptr, true, 0, native_set_startup, native_get_startup, 0},
	{nativeliterals::type.data(), REG_SZ, nullptr, true, 0, native_set_type, native_get_type, 0},
	{nullptr, nullptr, nullptr, nullptr, nullptr}
};
