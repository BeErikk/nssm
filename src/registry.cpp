/*******************************************************************************
 registry.cpp - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#include "nssm_pch.h"
#include "common.h"

#include "registry.h"

extern const wchar_t* exit_action_strings[];

static int32_t service_registry_path(const wchar_t* service_name, bool parameters, const wchar_t* sub, wchar_t* buffer, uint32_t buflen)
{
	int32_t ret;

	if (parameters)
	{
		if (sub)
			ret = ::_snwprintf_s(buffer, buflen, _TRUNCATE, regliterals::regpath.data() L"\\" regliterals::regparameters.data() L"\\%s", service_name, sub);
		else
			ret = ::_snwprintf_s(buffer, buflen, _TRUNCATE, regliterals::regpath.data() L"\\" regliterals::regparameters.data(), service_name);
	}
	else
		ret = ::_snwprintf_s(buffer, buflen, _TRUNCATE, regliterals::regpath.data(), service_name);

	return ret;
}

static int32_t open_registry_key(const wchar_t* registry, REGSAM sam, HKEY* key, bool must_exist)
{
	int32_t error;

	if (sam & KEY_SET_VALUE)
	{
		error = ::RegCreateKeyExW(HKEY_LOCAL_MACHINE, registry, 0, 0, REG_OPTION_NON_VOLATILE, sam, 0, key, 0);
		if (error != ERROR_SUCCESS)
		{
			*key = 0;
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, error_string(GetLastError()), 0);
			return error;
		}
	}
	else
	{
		error = ::RegOpenKeyExW(HKEY_LOCAL_MACHINE, registry, 0, sam, key);
		if (error != ERROR_SUCCESS)
		{
			*key = 0;
			if (error != ERROR_FILE_NOT_FOUND || must_exist)
				log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, error_string(GetLastError()), 0);
		}
	}

	return error;
}

static HKEY open_registry_key(const wchar_t* registry, REGSAM sam, bool must_exist)
{
	HKEY key;
	/*int32_t error = */open_registry_key(registry, sam, &key, must_exist);
	return key;
}

int32_t create_messages()
{
	HKEY key;

	wchar_t registry[KEY_LENGTH];
	if (::_snwprintf_s(registry, std::size(registry), _TRUNCATE, L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\%s", NSSM) < 0)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"eventlog registry", L"create_messages()", 0);
		return 1;
	}

	if (::RegCreateKeyExW(HKEY_LOCAL_MACHINE, registry, 0, 0, REG_OPTION_NON_VOLATILE, KEY_WRITE, 0, &key, 0) != ERROR_SUCCESS)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, error_string(GetLastError()), 0);
		return 2;
	}

	/* Get path of this program */
	const wchar_t* path = nssm_unquoted_imagepath();

	/* Try to register the module but don't worry so much on failure */
	::RegSetValueExW(key, L"EventMessageFile", 0, REG_SZ, (const uint8_t*)path, (uint32_t)(::wcslen(path) + 1) * sizeof(wchar_t));
	uint32_t types = EVENTLOG_INFORMATION_TYPE | EVENTLOG_WARNING_TYPE | EVENTLOG_ERROR_TYPE;
	::RegSetValueExW(key, L"TypesSupported", 0, REG_DWORD, (const uint8_t*)&types, sizeof(types));

	return 0;
}

int32_t enumerate_registry_values(HKEY key, uint32_t* index, wchar_t* name, uint32_t namelen)
{
	ULONG type;
	ULONG datalen = namelen;
	LSTATUS error = ::RegEnumValueW(key, *index, name, &datalen, 0, &type, 0, 0);
	if (error == ERROR_SUCCESS)
		++*index;
	return error;
}

int32_t create_parameters(nssm_service_t* service, bool editing)
{
	/* Try to open the registry */
	HKEY key = open_registry(service->name, KEY_WRITE);
	if (!key)
		return 1;

	/* Remember parameters in case we need to delete them. */
	wchar_t registry[KEY_LENGTH];
	int32_t ret = service_registry_path(service->name, true, 0, registry, std::size(registry));

	/* Try to create the parameters */
	if (set_expand_string(key, regliterals::regexe.data(), service->exe))
	{
		if (ret > 0)
			::RegDeleteKeyW(HKEY_LOCAL_MACHINE, registry);
		RegCloseKey(key);
		return 2;
	}
	if (set_expand_string(key, regliterals::regflags.data(), service->flags))
	{
		if (ret > 0)
			::RegDeleteKeyW(HKEY_LOCAL_MACHINE, registry);
		RegCloseKey(key);
		return 3;
	}
	if (set_expand_string(key, regliterals::regdir.data(), service->dir))
	{
		if (ret > 0)
			::RegDeleteKeyW(HKEY_LOCAL_MACHINE, registry);
		RegCloseKey(key);
		return 4;
	}

	/* Other non-default parameters. May fail. */
	if (service->priority != NORMAL_PRIORITY_CLASS)
		set_number(key, regliterals::regpriority, service->priority);
	else if (editing)
		::RegDeleteValueW(key, regliterals::regpriority);
	if (service->affinity)
	{
		wchar_t* string;
		if (!affinity_mask_to_string(service->affinity, &string))
		{
			if (::RegSetValueExW(key, regliterals::regaffinity, 0, REG_SZ, (const uint8_t*)string, (uint32_t)(::wcslen(string) + 1) * sizeof(wchar_t)) != ERROR_SUCCESS)
			{
				log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, regliterals::regaffinity, error_string(GetLastError()), 0);
				HeapFree(GetProcessHeap(), 0, string);
				return 5;
			}
		}
		if (string)
			HeapFree(GetProcessHeap(), 0, string);
	}
	else if (editing)
		::RegDeleteValueW(key, regliterals::regaffinity);
	uint32_t stop_method_skip = ~service->stop_method;
	if (stop_method_skip)
		set_number(key, regliterals::regstopmethodskip, stop_method_skip);
	else if (editing)
		::RegDeleteValueW(key, regliterals::regstopmethodskip);
	if (service->default_exit_action < exit::numactions)
		create_exit_action(service->name, exit_action_strings[service->default_exit_action], editing);
	if (service->restart_delay)
		set_number(key, regliterals::regrestartdelay, service->restart_delay);
	else if (editing)
		::RegDeleteValueW(key, regliterals::regrestartdelay);
	if (service->throttle_delay != wait::reset_throttle_restart)
		set_number(key, regliterals::regthrottle, service->throttle_delay);
	else if (editing)
		::RegDeleteValueW(key, regliterals::regthrottle);
	if (service->kill_console_delay != wait::kill_console_grace_period)
		set_number(key, regliterals::regkillconsolegraceperiod, service->kill_console_delay);
	else if (editing)
		::RegDeleteValueW(key, regliterals::regkillconsolegraceperiod);
	if (service->kill_window_delay != wait::kill_window_grace_period)
		set_number(key, regliterals::regkillwindowgraceperiod, service->kill_window_delay);
	else if (editing)
		::RegDeleteValueW(key, regliterals::regkillwindowgraceperiod);
	if (service->kill_threads_delay != wait::kill_threads_grace_period)
		set_number(key, regliterals::regkillthreadsgraceperiod, service->kill_threads_delay);
	else if (editing)
		::RegDeleteValueW(key, regliterals::regkillthreadsgraceperiod);
	if (!service->kill_process_tree)
		set_number(key, regliterals::regkillprocesstree, 0);
	else if (editing)
		::RegDeleteValueW(key, regliterals::regkillprocesstree);
	if (service->stdin_path[0] || editing)
	{
		if (service->stdin_path[0])
			set_expand_string(key, regliterals::regstdin, service->stdin_path);
		else if (editing)
			::RegDeleteValueW(key, regliterals::regstdin);
		if (service->stdin_sharing != NSSM_STDIN_SHARING)
			set_createfile_parameter(key, regliterals::regstdin, regliterals::regsharemode, service->stdin_sharing);
		else if (editing)
			delete_createfile_parameter(key, regliterals::regstdin, regliterals::regsharemode);
		if (service->stdin_disposition != NSSM_STDIN_DISPOSITION)
			set_createfile_parameter(key, regliterals::regstdin, regliterals::regdisposition, service->stdin_disposition);
		else if (editing)
			delete_createfile_parameter(key, regliterals::regstdin, regliterals::regdisposition);
		if (service->stdin_flags != NSSM_STDIN_FLAGS)
			set_createfile_parameter(key, regliterals::regstdin, regliterals::regstdioflags, service->stdin_flags);
		else if (editing)
			delete_createfile_parameter(key, regliterals::regstdin, regliterals::regstdioflags);
	}
	if (service->stdout_path[0] || editing)
	{
		if (service->stdout_path[0])
			set_expand_string(key, regliterals::regstdout, service->stdout_path);
		else if (editing)
			::RegDeleteValueW(key, regliterals::regstdout);
		if (service->stdout_sharing != NSSM_STDOUT_SHARING)
			set_createfile_parameter(key, regliterals::regstdout, regliterals::regsharemode, service->stdout_sharing);
		else if (editing)
			delete_createfile_parameter(key, regliterals::regstdout, regliterals::regsharemode);
		if (service->stdout_disposition != NSSM_STDOUT_DISPOSITION)
			set_createfile_parameter(key, regliterals::regstdout, regliterals::regdisposition, service->stdout_disposition);
		else if (editing)
			delete_createfile_parameter(key, regliterals::regstdout, regliterals::regdisposition);
		if (service->stdout_flags != NSSM_STDOUT_FLAGS)
			set_createfile_parameter(key, regliterals::regstdout, regliterals::regstdioflags, service->stdout_flags);
		else if (editing)
			delete_createfile_parameter(key, regliterals::regstdout, regliterals::regstdioflags);
		if (service->stdout_copy_and_truncate)
			set_createfile_parameter(key, regliterals::regstdout, regliterals::regcopytruncate, 1);
		else if (editing)
			delete_createfile_parameter(key, regliterals::regstdout, regliterals::regcopytruncate);
	}
	if (service->stderr_path[0] || editing)
	{
		if (service->stderr_path[0])
			set_expand_string(key, regliterals::regstderr, service->stderr_path);
		else if (editing)
			::RegDeleteValueW(key, regliterals::regstderr);
		if (service->stderr_sharing != NSSM_STDERR_SHARING)
			set_createfile_parameter(key, regliterals::regstderr, regliterals::regsharemode, service->stderr_sharing);
		else if (editing)
			delete_createfile_parameter(key, regliterals::regstderr, regliterals::regsharemode);
		if (service->stderr_disposition != NSSM_STDERR_DISPOSITION)
			set_createfile_parameter(key, regliterals::regstderr, regliterals::regdisposition, service->stderr_disposition);
		else if (editing)
			delete_createfile_parameter(key, regliterals::regstderr, regliterals::regdisposition);
		if (service->stderr_flags != NSSM_STDERR_FLAGS)
			set_createfile_parameter(key, regliterals::regstderr, regliterals::regstdioflags, service->stderr_flags);
		else if (editing)
			delete_createfile_parameter(key, regliterals::regstderr, regliterals::regstdioflags);
		if (service->stderr_copy_and_truncate)
			set_createfile_parameter(key, regliterals::regstderr, regliterals::regcopytruncate, 1);
		else if (editing)
			delete_createfile_parameter(key, regliterals::regstderr, regliterals::regcopytruncate);
	}
	if (service->timestamp_log)
		set_number(key, regliterals::regtimestamplog, 1);
	else if (editing)
		::RegDeleteValueW(key, regliterals::regtimestamplog);
	if (service->hook_share_output_handles)
		set_number(key, regliterals::regredirecthook, 1);
	else if (editing)
		::RegDeleteValueW(key, regliterals::regredirecthook);
	if (service->rotate_files)
		set_number(key, regliterals::regrotate, 1);
	else if (editing)
		::RegDeleteValueW(key, regliterals::regrotate);
	if (service->rotate_stdout_online)
		set_number(key, regliterals::regrotateonline, 1);
	else if (editing)
		::RegDeleteValueW(key, regliterals::regrotateonline);
	if (service->rotate_seconds)
		set_number(key, regliterals::regrotateseconds, service->rotate_seconds);
	else if (editing)
		::RegDeleteValueW(key, regliterals::regrotateseconds);
	if (service->rotate_bytes_low)
		set_number(key, regliterals::regrotatebyteslow, service->rotate_bytes_low);
	else if (editing)
		::RegDeleteValueW(key, regliterals::regrotatebyteslow);
	if (service->rotate_bytes_high)
		set_number(key, regliterals::regrotatebyteshigh, service->rotate_bytes_high);
	else if (editing)
		::RegDeleteValueW(key, regliterals::regrotatebyteshigh);
	if (service->rotate_delay != wait::rotatedelay)
		set_number(key, regliterals::regrotatedelay, service->rotate_delay);
	else if (editing)
		::RegDeleteValueW(key, regliterals::regrotatedelay);
	if (service->no_console)
		set_number(key, regliterals::regnoconsole, 1);
	else if (editing)
		::RegDeleteValueW(key, regliterals::regnoconsole);

	/* Environment */
	if (service->env)
	{
		if (::RegSetValueExW(key, regliterals::regenv.data(), 0, REG_MULTI_SZ, (const uint8_t*)service->env, (uint32_t)service->envlen * sizeof(wchar_t)) != ERROR_SUCCESS)
		{
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, regliterals::regenv.data(), error_string(GetLastError()), 0);
		}
	}
	else if (editing)
		::RegDeleteValueW(key, regliterals::regenv.data());
	if (service->env_extra)
	{
		if (::RegSetValueExW(key, regliterals::regenvextra.data(), 0, REG_MULTI_SZ, (const uint8_t*)service->env_extra, (uint32_t)service->env_extralen * sizeof(wchar_t)) != ERROR_SUCCESS)
		{
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, regliterals::regenvextra.data(), error_string(GetLastError()), 0);
		}
	}
	else if (editing)
		::RegDeleteValueW(key, regliterals::regenvextra.data());

	/* Close registry. */
	RegCloseKey(key);

	return 0;
}

int32_t create_exit_action(wchar_t* service_name, const wchar_t* action_string, bool editing)
{
	/* Get registry */
	wchar_t registry[KEY_LENGTH];
	if (service_registry_path(service_name, true, regliterals::regexit.data(), registry, std::size(registry)) < 0)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"regliterals::regexit", L"create_exit_action()", 0);
		return 1;
	}

	/* Try to open the registry */
	HKEY key;
	uint32_t disposition;
	if (::RegCreateKeyExW(HKEY_LOCAL_MACHINE, registry, 0, 0, REG_OPTION_NON_VOLATILE, KEY_WRITE, 0, &key, &disposition) != ERROR_SUCCESS)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, error_string(GetLastError()), 0);
		return 2;
	}

	/* Do nothing if the key already existed */
	if (disposition == REG_OPENED_EXISTING_KEY && !editing)
	{
		RegCloseKey(key);
		return 0;
	}

	/* Create the default value */
	if (::RegSetValueExW(key, 0, 0, REG_SZ, (const uint8_t*)action_string, (uint32_t)(::wcslen(action_string) + 1) * sizeof(wchar_t)) != ERROR_SUCCESS)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, regliterals::regexit.data(), error_string(GetLastError()), 0);
		RegCloseKey(key);
		return 3;
	}

	/* Close registry */
	RegCloseKey(key);

	return 0;
}

int32_t get_environment(wchar_t* service_name, HKEY key, wchar_t* value, wchar_t** env, uint32_t* envlen)
{
	uint32_t type = REG_MULTI_SZ;
	uint32_t envsize;

	*envlen = 0;

	/* Dummy test to find buffer size */
	uint32_t ret = ::RegQueryValueExW(key, value, 0, &type, nullptr, &envsize);
	if (ret != ERROR_SUCCESS)
	{
		*env = 0;
		/* The service probably doesn't have any environment configured */
		if (ret == ERROR_FILE_NOT_FOUND)
			return 0;
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(ret), 0);
		return 1;
	}

	if (type != REG_MULTI_SZ)
	{
		log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_INVALID_ENVIRONMENT_STRING_TYPE, value, service_name, 0);
		*env = 0;
		return 2;
	}

	/* Minimum usable environment would be A= nullptr nullptr. */
	if (envsize < 4 * sizeof(wchar_t))
	{
		*env = 0;
		return 3;
	}

	/* Previously initialised? */
	if (*env)
		HeapFree(GetProcessHeap(), 0, *env);

	*env = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, envsize);
	if (!*env)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, value, L"get_environment()", 0);
		return 4;
	}

	/* Actually get the strings. */
	ret = ::RegQueryValueExW(key, value, 0, &type, (uint8_t*)*env, &envsize);
	if (ret != ERROR_SUCCESS)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(ret), 0);
		HeapFree(GetProcessHeap(), 0, *env);
		*env = 0;
		return 5;
	}

	/* Value retrieved by ::RegQueryValueExW() is SIZE not COUNT. */
	*envlen = (uint32_t)environment_length(*env);

	return 0;
}

int32_t get_string(HKEY key, wchar_t* value, wchar_t* data, uint32_t datalen, bool expand, bool sanitise, bool must_exist)
{
	wchar_t* buffer = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, datalen);
	if (!buffer)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, value, L"get_string()", 0);
		return 1;
	}

	ZeroMemory(data, datalen);

	uint32_t type = REG_EXPAND_SZ;
	uint32_t buflen = datalen;

	uint32_t ret = ::RegQueryValueExW(key, value, 0, &type, (uint8_t*)buffer, &buflen);
	if (ret != ERROR_SUCCESS)
	{
		HeapFree(GetProcessHeap(), 0, buffer);

		if (ret == ERROR_FILE_NOT_FOUND)
		{
			if (!must_exist)
				return 0;
		}

		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(ret), 0);
		return 2;
	}

	/* Paths aren't allowed to contain quotes. */
	if (sanitise)
		::PathUnquoteSpacesW(buffer);

	/* Do we want to expand the string? */
	if (!expand)
	{
		if (type == REG_EXPAND_SZ)
			type = REG_SZ;
	}

	/* Technically we shouldn't expand environment strings from REG_SZ values */
	if (type != REG_EXPAND_SZ)
	{
		memmove(data, buffer, buflen);
		HeapFree(GetProcessHeap(), 0, buffer);
		return 0;
	}

	ret = ExpandEnvironmentStrings((wchar_t*)buffer, data, datalen);
	if (!ret || ret > datalen)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_EXPANDENVIRONMENTSTRINGS_FAILED, buffer, error_string(GetLastError()), 0);
		HeapFree(GetProcessHeap(), 0, buffer);
		return 3;
	}

	HeapFree(GetProcessHeap(), 0, buffer);
	return 0;
}

int32_t get_string(HKEY key, wchar_t* value, wchar_t* data, uint32_t datalen, bool sanitise)
{
	return get_string(key, value, data, datalen, false, sanitise, true);
}

int32_t expand_parameter(HKEY key, wchar_t* value, wchar_t* data, uint32_t datalen, bool sanitise, bool must_exist)
{
	return get_string(key, value, data, datalen, true, sanitise, must_exist);
}

int32_t expand_parameter(HKEY key, wchar_t* value, wchar_t* data, uint32_t datalen, bool sanitise)
{
	return expand_parameter(key, value, data, datalen, sanitise, true);
}

/*
  Sets a string in the registry.
  Returns: 0 if it was set.
           1 on error.
*/
int32_t set_string(HKEY key, wchar_t* value, wchar_t* string, bool expand)
{
	uint32_t type = expand ? REG_EXPAND_SZ : REG_SZ;
	if (::RegSetValueExW(key, value, 0, type, (const uint8_t*)string, (uint32_t)(::wcslen(string) + 1) * sizeof(wchar_t)) == ERROR_SUCCESS)
		return 0;
	log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, value, error_string(GetLastError()), 0);
	return 1;
}

int32_t set_string(HKEY key, const wchar_t* value, wchar_t* string)
{
	return set_string(key, value, string, false);
}

int32_t set_expand_string(HKEY key, const wchar_t* value, wchar_t* string)
{
	return set_string(key, value, string, true);
}

/*
  Set an uint32_t in the registry.
  Returns: 0 if it was set.
           1 on error.
*/
int32_t set_number(HKEY key, wchar_t* value, uint32_t number)
{
	if (::RegSetValueExW(key, value, 0, REG_DWORD, (const uint8_t*)&number, sizeof(number)) == ERROR_SUCCESS)
		return 0;
	log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, value, error_string(GetLastError()), 0);
	return 1;
}

/*
  Query an uint32_t from the registry.
  Returns:  1 if a number was retrieved.
            0 if none was found and must_exist is false.
           -1 if none was found and must_exist is true.
           -2 otherwise.
*/
int32_t get_number(HKEY key, wchar_t* value, uint32_t* number, bool must_exist)
{
	uint32_t type = REG_DWORD;
	uint32_t number_len = sizeof(uint32_t);

	int32_t ret = ::RegQueryValueExW(key, value, 0, &type, (uint8_t*)number, &number_len);
	if (ret == ERROR_SUCCESS)
		return 1;

	if (ret == ERROR_FILE_NOT_FOUND)
	{
		if (!must_exist)
			return 0;
	}

	log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(ret), 0);
	if (ret == ERROR_FILE_NOT_FOUND)
		return -1;

	return -2;
}

int32_t get_number(HKEY key, wchar_t* value, uint32_t* number)
{
	return get_number(key, value, number, true);
}

/* Replace nullptr with CRLF. Leave nullptr nullptr as the end marker. */
int32_t format_double_null(wchar_t* dn, uint32_t dnlen, wchar_t** formatted, uint32_t* newlen)
{
	uint32_t i, j;
	*newlen = dnlen;

	if (!*newlen)
	{
		*formatted = 0;
		return 0;
	}

	for (i = 0; i < dnlen; i++)
		if (!dn[i] && dn[i + 1])
			++*newlen;

	*formatted = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, *newlen * sizeof(wchar_t));
	if (!*formatted)
	{
		*newlen = 0;
		return 1;
	}

	for (i = 0, j = 0; i < dnlen; i++)
	{
		(*formatted)[j] = dn[i];
		if (!dn[i])
		{
			if (dn[i + 1])
			{
				(*formatted)[j] = _T('\r');
				(*formatted)[++j] = _T('\n');
			}
		}
		j++;
	}

	return 0;
}

/* Strip CR and replace LF with nullptr.  */
int32_t unformat_double_null(wchar_t* formatted, uint32_t formattedlen, wchar_t** dn, uint32_t* newlen)
{
	uint32_t i, j;
	*newlen = 0;

	/* Don't count trailing NULLs. */
	for (i = 0; i < formattedlen; i++)
	{
		if (!formatted[i])
		{
			formattedlen = i;
			break;
		}
	}

	if (!formattedlen)
	{
		*dn = 0;
		return 0;
	}

	for (i = 0; i < formattedlen; i++)
		if (formatted[i] != _T('\r'))
			++*newlen;

	/* Skip blank lines. */
	for (i = 0; i < formattedlen; i++)
	{
		if (formatted[i] == _T('\r') && formatted[i + 1] == _T('\n'))
		{
			/* This is the last CRLF. */
			if (i >= formattedlen - 2)
				break;

			/*
        Strip at the start of the block or if the next characters are
        CRLF too.
      */
			if (!i || (formatted[i + 2] == _T('\r') && formatted[i + 3] == _T('\n')))
			{
				for (j = i + 2; j < formattedlen; j++)
					formatted[j - 2] = formatted[j];
				formatted[formattedlen--] = _T('\0');
				formatted[formattedlen--] = _T('\0');
				i--;
				--*newlen;
			}
		}
	}

	/* Must end with two NULLs. */
	*newlen += 2;

	*dn = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, *newlen * sizeof(wchar_t));
	if (!*dn)
		return 1;

	for (i = 0, j = 0; i < formattedlen; i++)
	{
		if (formatted[i] == _T('\r'))
			continue;
		if (formatted[i] == _T('\n'))
			(*dn)[j] = _T('\0');
		else
			(*dn)[j] = formatted[i];
		j++;
	}

	return 0;
}

/* Copy a block. */
int32_t copy_double_null(wchar_t* dn, uint32_t dnlen, wchar_t** newdn)
{
	if (!newdn)
		return 1;

	*newdn = 0;
	if (!dn)
		return 0;

	*newdn = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, dnlen * sizeof(wchar_t));
	if (!*newdn)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"dn", L"copy_double_null()", 0);
		return 2;
	}

	memmove(*newdn, dn, dnlen * sizeof(wchar_t));
	return 0;
}

/*
  Create a new block with all the strings of the first block plus a new string.
  The new string may be specified as <key> <delimiter> <value> and the keylen
  gives the offset into the string to compare against existing entries.
  If the key is already present its value will be overwritten in place.
  If the key is blank or empty the new block will still be allocated and have
  non-zero length.
*/
int32_t append_to_double_null(wchar_t* dn, uint32_t dnlen, wchar_t** newdn, uint32_t* newlen, wchar_t* append, size_t keylen, bool case_sensitive)
{
	if (!append || !append[0])
		return copy_double_null(dn, dnlen, newdn);
	size_t appendlen = ::wcslen(append);
	int32_t (*fn)(const wchar_t*, const wchar_t*, size_t) = (case_sensitive) ? wcsncmp : _wcsnicmp;

	/* Identify the key, if any, or treat the whole string as the key. */
	wchar_t* key = 0;
	if (!keylen || keylen > appendlen)
		keylen = appendlen;
	key = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (keylen + 1) * sizeof(wchar_t));
	if (!key)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"key", L"append_to_double_null()", 0);
		return 1;
	}
	memmove(key, append, keylen * sizeof(wchar_t));
	key[keylen] = _T('\0');

	/* Find the length of the block not including any existing key. */
	size_t len = 0;
	wchar_t* s;
	for (s = dn; *s; s++)
	{
		if (fn(s, key, keylen))
			len += ::wcslen(s) + 1;
		for (; *s; s++)
			;
	}

	/* Account for new entry. */
	len += ::wcslen(append) + 1;

	/* Account for trailing nullptr. */
	len++;

	/* Allocate a new block. */
	*newdn = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len * sizeof(wchar_t));
	if (!*newdn)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"newdn", L"append_to_double_null()", 0);
		HeapFree(GetProcessHeap(), 0, key);
		return 2;
	}

	/* Copy existing entries.*/
	*newlen = (uint32_t)len;
	wchar_t* t = *newdn;
	wchar_t* u;
	bool replaced = false;
	for (s = dn; *s; s++)
	{
		if (fn(s, key, keylen))
			u = s;
		else
		{
			u = append;
			replaced = true;
		}
		len = ::wcslen(u) + 1;
		memmove(t, u, len * sizeof(wchar_t));
		t += len;
		for (; *s; s++)
			;
	}

	/* Add the entry if it wasn't already replaced.  The buffer was zeroed. */
	if (!replaced)
		memmove(t, append, ::wcslen(append) * sizeof(wchar_t));

	HeapFree(GetProcessHeap(), 0, key);
	return 0;
}

/*
  Create a new block with all the string of the first block minus the given
  string.
  The keylen parameter gives the offset into the string to compare against
  existing entries.  If a substring of existing value matches the string to
  the given length it will be removed.
  If the last entry is removed the new block will still be allocated and
  have non-zero length.
*/
int32_t remove_from_double_null(wchar_t* dn, uint32_t dnlen, wchar_t** newdn, uint32_t* newlen, wchar_t* remove, size_t keylen, bool case_sensitive)
{
	if (!remove || !remove[0])
		return copy_double_null(dn, dnlen, newdn);
	size_t removelen = ::wcslen(remove);
	int32_t (*fn)(const wchar_t*, const wchar_t*, size_t) = (case_sensitive) ? wcsncmp : _wcsnicmp;

	/* Identify the key, if any, or treat the whole string as the key. */
	wchar_t* key = 0;
	if (!keylen || keylen > removelen)
		keylen = removelen;
	key = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (keylen + 1) * sizeof(wchar_t));
	if (!key)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"key", L"remove_from_double_null()", 0);
		return 1;
	}
	memmove(key, remove, keylen * sizeof(wchar_t));
	key[keylen] = _T('\0');

	/* Find the length of the block not including any existing key. */
	size_t len = 0;
	wchar_t* s;
	for (s = dn; *s; s++)
	{
		if (fn(s, key, keylen))
			len += ::wcslen(s) + 1;
		for (; *s; s++)
			;
	}

	/* Account for trailing nullptr. */
	if (++len < 2)
		len = 2;

	/* Allocate a new block. */
	*newdn = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len * sizeof(wchar_t));
	if (!*newdn)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"newdn", L"remove_from_double_null()", 0);
		HeapFree(GetProcessHeap(), 0, key);
		return 2;
	}

	/* Copy existing entries.*/
	*newlen = (uint32_t)len;
	wchar_t* t = *newdn;
	for (s = dn; *s; s++)
	{
		if (fn(s, key, keylen))
		{
			len = ::wcslen(s) + 1;
			memmove(t, s, len * sizeof(wchar_t));
			t += len;
		}
		for (; *s; s++)
			;
	}

	HeapFree(GetProcessHeap(), 0, key);
	return 0;
}

void override_milliseconds(wchar_t* service_name, HKEY key, wchar_t* value, uint32_t* buffer, uint32_t default_value, uint32_t event)
{
	uint32_t type = REG_DWORD;
	uint32_t buflen = sizeof(uint32_t);
	bool ok = false;
	uint32_t ret = ::RegQueryValueExW(key, value, 0, &type, (uint8_t*)buffer, &buflen);
	if (ret != ERROR_SUCCESS)
	{
		if (ret != ERROR_FILE_NOT_FOUND)
		{
			if (type != REG_DWORD)
			{
				wchar_t milliseconds[16];
				::_snwprintf_s(milliseconds, std::size(milliseconds), _TRUNCATE, L"%u", default_value);
				log_event(EVENTLOG_WARNING_TYPE, event, service_name, value, milliseconds, 0);
			}
			else
				log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(ret), 0);
		}
	}
	else
		ok = true;

	if (!ok)
		*buffer = default_value;
}

/* Open the key of the service itself Services\<service_name>. */
HKEY open_service_registry(const wchar_t* service_name, REGSAM sam, bool must_exist)
{
	/* Get registry */
	wchar_t registry[KEY_LENGTH];
	if (service_registry_path(service_name, false, 0, registry, std::size(registry)) < 0)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, regliterals::regpath.data(), L"open_service_registry()", 0);
		return 0;
	}

	return open_registry_key(registry, sam, must_exist);
}

/* Open a subkey of the service Services\<service_name>\<sub>. */
int32_t open_registry(const wchar_t* service_name, const wchar_t* sub, REGSAM sam, HKEY* key, bool must_exist)
{
	/* Get registry */
	wchar_t registry[KEY_LENGTH];
	if (service_registry_path(service_name, true, sub, registry, std::size(registry)) < 0)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, regliterals::regpath.data(), L"open_registry()", 0);
		return 0;
	}

	return open_registry_key(registry, sam, key, must_exist);
}

HKEY open_registry(const wchar_t* service_name, const wchar_t* sub, REGSAM sam, bool must_exist)
{
	HKEY key;
	int32_t error = open_registry(service_name, sub, sam, &key, must_exist);
	return key;
}

HKEY open_registry(const wchar_t* service_name, const wchar_t* sub, REGSAM sam)
{
	return open_registry(service_name, sub, sam, true);
}

HKEY open_registry(const wchar_t* service_name, REGSAM sam)
{
	return open_registry(service_name, 0, sam, true);
}

int32_t get_io_parameters(nssm_service_t* service, HKEY key)
{
	/* stdin */
	if (get_createfile_parameters(key, regliterals::regstdin, service->stdin_path, &service->stdin_sharing, NSSM_STDIN_SHARING, &service->stdin_disposition, NSSM_STDIN_DISPOSITION, &service->stdin_flags, NSSM_STDIN_FLAGS, 0))
	{
		service->stdin_sharing = service->stdin_disposition = service->stdin_flags = 0;
		ZeroMemory(service->stdin_path, std::size(service->stdin_path) * sizeof(wchar_t));
		return 1;
	}

	/* stdout */
	if (get_createfile_parameters(key, regliterals::regstdout, service->stdout_path, &service->stdout_sharing, NSSM_STDOUT_SHARING, &service->stdout_disposition, NSSM_STDOUT_DISPOSITION, &service->stdout_flags, NSSM_STDOUT_FLAGS, &service->stdout_copy_and_truncate))
	{
		service->stdout_sharing = service->stdout_disposition = service->stdout_flags = 0;
		ZeroMemory(service->stdout_path, std::size(service->stdout_path) * sizeof(wchar_t));
		return 2;
	}

	/* stderr */
	if (get_createfile_parameters(key, regliterals::regstderr, service->stderr_path, &service->stderr_sharing, NSSM_STDERR_SHARING, &service->stderr_disposition, NSSM_STDERR_DISPOSITION, &service->stderr_flags, NSSM_STDERR_FLAGS, &service->stderr_copy_and_truncate))
	{
		service->stderr_sharing = service->stderr_disposition = service->stderr_flags = 0;
		ZeroMemory(service->stderr_path, std::size(service->stderr_path) * sizeof(wchar_t));
		return 3;
	}

	return 0;
}

int32_t get_parameters(nssm_service_t* service, STARTUPINFOW* si)
{
	uint32_t ret;

	/* Try to open the registry */
	HKEY key = open_registry(service->name, KEY_READ);
	if (!key)
		return 1;

	/* Don't expand parameters when retrieving for the GUI. */
	bool expand = si ? true : false;

	/* Try to get environment variables - may fail */
	get_environment(service->name, key, regliterals::regenv.data(), &service->env, &service->envlen);
	/* Environment variables to add to existing rather than replace - may fail. */
	get_environment(service->name, key, regliterals::regenvextra.data(), &service->env_extra, &service->env_extralen);

	/* Set environment if we are starting the service. */
	if (si)
		set_service_environment(service);

	/* Try to get executable file - MUST succeed */
	if (get_string(key, regliterals::regexe.data(), service->exe, sizeof(service->exe), expand, false, true))
	{
		RegCloseKey(key);
		return 3;
	}

	/* Try to get flags - may fail and we don't care */
	if (get_string(key, regliterals::regflags.data(), service->flags, sizeof(service->flags), expand, false, true))
	{
		log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_NO_FLAGS, regliterals::regflags.data(), service->name, service->exe, 0);
		ZeroMemory(service->flags, sizeof(service->flags));
	}

	/* Try to get startup directory - may fail and we fall back to a default */
	if (get_string(key, regliterals::regdir.data(), service->dir, sizeof(service->dir), expand, true, true) || !service->dir[0])
	{
		::_snwprintf_s(service->dir, std::size(service->dir), _TRUNCATE, L"%s", service->exe);
		strip_basename(service->dir);
		if (service->dir[0] == _T('\0'))
		{
			/* Help! */
			ret = ::GetWindowsDirectoryW(service->dir, sizeof(service->dir));
			if (!ret || ret > sizeof(service->dir))
			{
				log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_NO_DIR_AND_NO_FALLBACK, regliterals::regdir.data(), service->name, 0);
				RegCloseKey(key);
				return 4;
			}
		}
		log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_NO_DIR, regliterals::regdir.data(), service->name, service->dir, 0);
	}

	/* Try to get processor affinity - may fail. */
	wchar_t buffer[512];
	if (get_string(key, regliterals::regaffinity, buffer, sizeof(buffer), false, false, false) || !buffer[0])
		service->affinity = 0LL;
	else if (affinity_string_to_mask(buffer, &service->affinity))
	{
		log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_BOGUS_AFFINITY_MASK, service->name, buffer);
		service->affinity = 0LL;
	}
	else
	{
		uintptr_t affinity, system_affinity;

		if (GetProcessAffinityMask(GetCurrentProcess(), &affinity, &system_affinity))
		{
			int64_t effective_affinity = service->affinity & system_affinity;
			if (effective_affinity != service->affinity)
			{
				wchar_t* system = 0;
				if (!affinity_mask_to_string(system_affinity, &system))
				{
					wchar_t* effective = 0;
					if (!affinity_mask_to_string(effective_affinity, &effective))
					{
						log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_EFFECTIVE_AFFINITY_MASK, service->name, buffer, system, effective, 0);
					}
					HeapFree(GetProcessHeap(), 0, effective);
				}
				HeapFree(GetProcessHeap(), 0, system);
			}
		}
	}

	/* Try to get priority - may fail. */
	uint32_t priority;
	if (get_number(key, regliterals::regpriority, &priority, false) == 1)
	{
		if (priority == (priority & priority_mask()))
			service->priority = priority;
		else
			log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_BOGUS_PRIORITY, service->name, regliterals::regpriority, 0);
	}

	/* Try to get hook I/O sharing - may fail. */
	uint32_t hook_share_output_handles;
	if (get_number(key, regliterals::regredirecthook, &hook_share_output_handles, false) == 1)
	{
		if (hook_share_output_handles)
			service->hook_share_output_handles = true;
		else
			service->hook_share_output_handles = false;
	}
	else
		hook_share_output_handles = false;
	/* Try to get file rotation settings - may fail. */
	uint32_t rotate_files;
	if (get_number(key, regliterals::regrotate, &rotate_files, false) == 1)
	{
		if (rotate_files)
			service->rotate_files = true;
		else
			service->rotate_files = false;
	}
	else
		service->rotate_files = false;
	if (get_number(key, regliterals::regrotateonline, &rotate_files, false) == 1)
	{
		if (rotate_files)
			service->rotate_stdout_online = service->rotate_stderr_online = true;
		else
			service->rotate_stdout_online = service->rotate_stderr_online = false;
	}
	else
		service->rotate_stdout_online = service->rotate_stderr_online = false;
	/* Log timestamping requires a logging thread.*/
	uint32_t timestamp_log;
	if (get_number(key, regliterals::regtimestamplog, &timestamp_log, false) == 1)
	{
		if (timestamp_log)
			service->timestamp_log = true;
		else
			service->timestamp_log = false;
	}
	else
		service->timestamp_log = false;

	/* Hook I/O sharing and online rotation need a pipe. */
	service->use_stdout_pipe = service->rotate_stdout_online || service->timestamp_log || hook_share_output_handles;
	service->use_stderr_pipe = service->rotate_stderr_online || service->timestamp_log || hook_share_output_handles;
	if (get_number(key, regliterals::regrotateseconds, &service->rotate_seconds, false) != 1)
		service->rotate_seconds = 0;
	if (get_number(key, regliterals::regrotatebyteslow, &service->rotate_bytes_low, false) != 1)
		service->rotate_bytes_low = 0;
	if (get_number(key, regliterals::regrotatebyteshigh, &service->rotate_bytes_high, false) != 1)
		service->rotate_bytes_high = 0;
	override_milliseconds(service->name, key, regliterals::regrotatedelay, &service->rotate_delay, wait::rotatedelay, NSSM_EVENT_BOGUS_THROTTLE);

	/* Try to get force new console setting - may fail. */
	if (get_number(key, regliterals::regnoconsole, &service->no_console, false) != 1)
		service->no_console = 0;

	/* Change to startup directory in case stdout/stderr are relative paths. */
	wchar_t cwd[nssmconst::pathlength];
	GetCurrentDirectory(std::size(cwd), cwd);
	SetCurrentDirectory(service->dir);

	/* Try to get stdout and stderr */
	if (get_io_parameters(service, key))
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GET_OUTPUT_HANDLES_FAILED, service->name, 0);
		RegCloseKey(key);
		SetCurrentDirectory(cwd);
		return 5;
	}

	/* Change back in case the startup directory needs to be deleted. */
	SetCurrentDirectory(cwd);

	/* Try to get mandatory restart delay */
	override_milliseconds(service->name, key, regliterals::regrestartdelay, &service->restart_delay, 0, NSSM_EVENT_BOGUS_RESTART_DELAY);

	/* Try to get throttle restart delay */
	override_milliseconds(service->name, key, regliterals::regthrottle, &service->throttle_delay, wait::reset_throttle_restart, NSSM_EVENT_BOGUS_THROTTLE);

	/* Try to get service stop flags. */
	uint32_t type = REG_DWORD;
	uint32_t stop_method_skip;
	uint32_t buflen = sizeof(stop_method_skip);
	bool stop_ok = false;
	ret = ::RegQueryValueExW(key, regliterals::regstopmethodskip, 0, &type, (uint8_t*)&stop_method_skip, &buflen);
	if (ret != ERROR_SUCCESS)
	{
		if (ret != ERROR_FILE_NOT_FOUND)
		{
			if (type != REG_DWORD)
			{
				log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_BOGUS_STOP_METHOD_SKIP, service->name, regliterals::regstopmethodskip, NSSM, 0);
			}
			else
				log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, regliterals::regstopmethodskip, error_string(ret), 0);
		}
	}
	else
		stop_ok = true;

	/* Try all methods except those requested to be skipped. */
	service->stop_method = ~0;
	if (stop_ok)
		service->stop_method &= ~stop_method_skip;

	/* Try to get kill delays - may fail. */
	override_milliseconds(service->name, key, regliterals::regkillconsolegraceperiod, &service->kill_console_delay, wait::kill_console_grace_period, NSSM_EVENT_BOGUS_KILL_CONSOLE_GRACE_PERIOD);
	override_milliseconds(service->name, key, regliterals::regkillwindowgraceperiod, &service->kill_window_delay, wait::kill_window_grace_period, NSSM_EVENT_BOGUS_KILL_WINDOW_GRACE_PERIOD);
	override_milliseconds(service->name, key, regliterals::regkillthreadsgraceperiod, &service->kill_threads_delay, wait::kill_threads_grace_period, NSSM_EVENT_BOGUS_KILL_THREADS_GRACE_PERIOD);

	/* Try to get process tree settings - may fail. */
	uint32_t kill_process_tree;
	if (get_number(key, regliterals::regkillprocesstree, &kill_process_tree, false) == 1)
	{
		if (kill_process_tree)
			service->kill_process_tree = true;
		else
			service->kill_process_tree = false;
	}
	else
		service->kill_process_tree = true;

	/* Try to get default exit action. */
	bool default_action;
	service->default_exit_action = exit::restart;
	wchar_t action_string[ACTION_LEN];
	if (!get_exit_action(service->name, 0, action_string, &default_action))
	{
		for (int32_t i = 0; exit_action_strings[i]; i++)
		{
			if (!_wcsnicmp((const wchar_t*)action_string, exit_action_strings[i], ACTION_LEN))
			{
				service->default_exit_action = i;
				break;
			}
		}
	}

	/* Close registry */
	RegCloseKey(key);

	return 0;
}

/*
  Sets the string for the exit action corresponding to the exit code.

  ret is a pointer to an uint32_t containing the exit code.
  If ret is nullptr, we retrieve the default exit action unconditionally.

  action is a buffer which receives the string.

  default_action is a pointer to a bool which is set to false if there
  was an explicit string for the given exit code, or true if we are
  returning the default action.

  Returns: 0 on success.
           1 on error.
*/
int32_t get_exit_action(const wchar_t* service_name, uint32_t* ret, wchar_t* action, bool* default_action)
{
	/* Are we returning the default action or a status-specific one? */
	*default_action = !ret;

	/* Try to open the registry */
	HKEY key = open_registry(service_name, regliterals::regexit.data(), KEY_READ);
	if (!key)
		return 1;

	uint32_t type = REG_SZ;
	uint32_t action_len = ACTION_LEN;

	wchar_t code[16];
	if (!ret)
		code[0] = _T('\0');
	else if (::_snwprintf_s(code, std::size(code), _TRUNCATE, L"%u", *ret) < 0)
	{
		RegCloseKey(key);
		return get_exit_action(service_name, 0, action, default_action);
	}
	if (::RegQueryValueExW(key, code, 0, &type, (uint8_t*)action, &action_len) != ERROR_SUCCESS)
	{
		RegCloseKey(key);
		/* Try again with * as the key if an exit code was defined */
		if (ret)
			return get_exit_action(service_name, 0, action, default_action);
		return 0;
	}

	/* Close registry */
	RegCloseKey(key);

	return 0;
}

int32_t set_hook(const wchar_t* service_name, const wchar_t* hook_event, const wchar_t* hook_action, wchar_t* cmd)
{
	/* Try to open the registry */
	wchar_t registry[KEY_LENGTH];
	if (::_snwprintf_s(registry, std::size(registry), _TRUNCATE, L"%s\\%s", regliterals::reghook, hook_event) < 0)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"hook registry", L"set_hook()", 0);
		return 1;
	}

	HKEY key;
	int32_t error;

	/* Don't create keys needlessly. */
	if (!::wcslen(cmd))
	{
		key = open_registry(service_name, registry, KEY_READ, false);
		if (!key)
			return 0;
		error = ::RegQueryValueExW(key, hook_action, 0, 0, 0, 0);
		RegCloseKey(key);
		if (error == ERROR_FILE_NOT_FOUND)
			return 0;
	}

	key = open_registry(service_name, registry, KEY_WRITE);
	if (!key)
		return 1;

	int32_t ret = 1;
	if (::wcslen(cmd))
		ret = set_string(key, (wchar_t*)hook_action, cmd, true);
	else
	{
		error = ::RegDeleteValueW(key, hook_action);
		if (error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND)
			ret = 0;
	}

	/* Close registry */
	RegCloseKey(key);

	return ret;
}

int32_t get_hook(const wchar_t* service_name, const wchar_t* hook_event, const wchar_t* hook_action, wchar_t* buffer, uint32_t buflen)
{
	/* Try to open the registry */
	wchar_t registry[KEY_LENGTH];
	if (::_snwprintf_s(registry, std::size(registry), _TRUNCATE, L"%s\\%s", regliterals::reghook, hook_event) < 0)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"hook registry", L"get_hook()", 0);
		return 1;
	}
	HKEY key;
	int32_t error = open_registry(service_name, registry, KEY_READ, &key, false);
	if (!key)
	{
		if (error == ERROR_FILE_NOT_FOUND)
		{
			ZeroMemory(buffer, buflen);
			return 0;
		}
		return 1;
	}

	int32_t ret = expand_parameter(key, (wchar_t*)hook_action, buffer, buflen, true, false);

	/* Close registry */
	RegCloseKey(key);

	return ret;
}
