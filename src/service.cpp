/*******************************************************************************
 service.cpp - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#include "nssm_pch.h"
#include "common.h"

#include "service.h"

bool is_admin;
bool use_critical_section;

extern imports_t imports;
extern settings_t settings[];

const wchar_t* exit_action_strings[] = {L"Restart", L"Ignore", L"Exit", L"Suicide", 0};
const wchar_t* startup_strings[] = {L"SERVICE_AUTO_START", L"SERVICE_DELAYED_AUTO_START", L"SERVICE_DEMAND_START", L"SERVICE_DISABLED", 0};
const wchar_t* priority_strings[] = {L"REALTIME_PRIORITY_CLASS", L"HIGH_PRIORITY_CLASS", L"ABOVE_NORMAL_PRIORITY_CLASS", L"NORMAL_PRIORITY_CLASS", L"BELOW_NORMAL_PRIORITY_CLASS", L"IDLE_PRIORITY_CLASS", 0};

static hook_thread_t hook_threads = {nullptr, 0};

typedef struct
{
	int32_t first;
	int32_t last;
} list_t;

/*
  Check the status in response to a control.
  Returns:  1 if the status is expected, eg STOP following CONTROL_STOP.
            0 if the status is desired, eg STOPPED following CONTROL_STOP.
           -1 if the status is undesired, eg STOPPED following CONTROL_START.
*/
static inline int32_t service_control_response(uint32_t control, uint32_t status)
{
	switch (control)
	{
	case wait::controlstart:
		switch (status)
		{
		case SERVICE_START_PENDING:
			return 1;

		case SERVICE_RUNNING:
			return 0;

		default:
			return -1;
		}

	case SERVICE_CONTROL_STOP:
	case SERVICE_CONTROL_SHUTDOWN:
		switch (status)
		{
		case SERVICE_RUNNING:
		case SERVICE_STOP_PENDING:
			return 1;

		case SERVICE_STOPPED:
			return 0;

		default:
			return -1;
		}

	case SERVICE_CONTROL_PAUSE:
		switch (status)
		{
		case SERVICE_PAUSE_PENDING:
			return 1;

		case SERVICE_PAUSED:
			return 0;

		default:
			return -1;
		}

	case SERVICE_CONTROL_CONTINUE:
		switch (status)
		{
		case SERVICE_CONTINUE_PENDING:
			return 1;

		case SERVICE_RUNNING:
			return 0;

		default:
			return -1;
		}

	case SERVICE_CONTROL_INTERROGATE:
	case wait::controlrotate:
		return 0;
	}

	return 0;
}

static inline int32_t await_service_control_response(uint32_t control, SC_HANDLE service_handle, SERVICE_STATUS* service_status, uint32_t initial_status, uint32_t cutoff)
{
	int32_t tries = 0;
	uint32_t checkpoint = 0;
	uint32_t waithint = 0;
	uint32_t waited = 0;
	while (QueryServiceStatus(service_handle, service_status))
	{
		int32_t response = service_control_response(control, service_status->dwCurrentState);
		/* Alas we can't WaitForSingleObject() on an SC_HANDLE. */
		if (!response)
			return response;
		if (response > 0 || service_status->dwCurrentState == initial_status)
		{
			if (service_status->dwCheckPoint != checkpoint || service_status->dwWaitHint != waithint)
				tries = 0;
			checkpoint = service_status->dwCheckPoint;
			waithint = service_status->dwWaitHint;
			if (++tries > 10)
				tries = 10;
			uint32_t wait = 50 * tries;
			if (cutoff)
			{
				if (waited > cutoff)
					return response;
				waited += wait;
			}
			Sleep(wait);
		}
		else
			return response;
	}
	return -1;
}

static inline int32_t await_service_control_response(uint32_t control, SC_HANDLE service_handle, SERVICE_STATUS* service_status, uint32_t initial_status)
{
	return await_service_control_response(control, service_handle, service_status, initial_status, 0);
}

static inline void wait_for_hooks(nssm_service_t* service, bool notify)
{
	SERVICE_STATUS_HANDLE status_handle;
	SERVICE_STATUS* status;

	/* On a clean shutdown we need to keep the service's status up-to-date. */
	if (notify)
	{
		status_handle = service->status_handle;
		status = &service->status;
	}
	else
	{
		status_handle = nullptr;
		status = nullptr;
	}

	EnterCriticalSection(&service->hook_section);
	await_hook_threads(&hook_threads, status_handle, status, wait::threaddeadline);
	LeaveCriticalSection(&service->hook_section);
}

int32_t affinity_mask_to_string(int64_t mask, wchar_t** string)
{
	if (!string)
		return 1;
	if (!mask)
	{
		*string = 0;
		return 0;
	}

	int64_t i, n;

	/* SetProcessAffinityMask() accepts a mask of up to 64 processors. */
	list_t set[64];
	for (n = 0; n < std::size(set); n++)
		set[n].first = set[n].last = -1;

	for (i = 0, n = 0; i < std::size(set); i++)
	{
		if (mask & (1LL << i))
		{
			if (set[n].first == -1)
				set[n].first = set[n].last = (int32_t)i;
			else if (set[n].last == (int32_t)i - 1)
				set[n].last = (int32_t)i;
			else
			{
				n++;
				set[n].first = set[n].last = (int32_t)i;
			}
		}
	}

	/* Worst case is 2x2 characters for first and last CPU plus - and/or , */
	size_t len = (size_t)(n + 1) * 6;
	*string = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len * sizeof(wchar_t));
	if (!string)
		return 2;

	size_t s = 0;
	int32_t ret;
	for (i = 0; i <= n; i++)
	{
		if (i)
			(*string)[s++] = _T(',');
		ret = ::_snwprintf_s(*string + s, 3, _TRUNCATE, L"%u", set[i].first);
		if (ret < 0)
		{
			HeapFree(GetProcessHeap(), 0, *string);
			*string = 0;
			return 3;
		}
		else
			s += ret;
		if (set[i].last != set[i].first)
		{
			ret = ::_snwprintf_s(*string + s, 4, _TRUNCATE, L"%c%u", (set[i].last == set[i].first + 1) ? _T(',') : _T('-'), set[i].last);
			if (ret < 0)
			{
				HeapFree(GetProcessHeap(), 0, *string);
				*string = 0;
				return 4;
			}
			else
				s += ret;
		}
	}

	return 0;
}

int32_t affinity_string_to_mask(wchar_t* string, int64_t* mask)
{
	if (!mask)
		return 1;

	*mask = 0LL;
	if (!string)
		return 0;

	list_t set[64];

	wchar_t* s = string;
	wchar_t* end;
	int32_t ret;
	int32_t i;
	int32_t n = 0;
	uint32_t number;

	for (n = 0; n < std::size(set); n++)
		set[n].first = set[n].last = -1;
	n = 0;

	while (*s)
	{
		ret = str_number(s, &number, &end);
		s = end;
		if (ret == 0 || ret == 2)
		{
			if (number >= std::size(set))
				return 2;
			set[n].first = set[n].last = (int32_t)number;

			switch (*s)
			{
			case 0:
				break;

			case _T(','):
				n++;
				s++;
				break;

			case _T('-'):
				if (!*(++s))
					return 3;
				ret = str_number(s, &number, &end);
				if (ret == 0 || ret == 2)
				{
					s = end;
					if (!*s || *s == _T(','))
					{
						set[n].last = (int32_t)number;
						if (!*s)
							break;
						n++;
						s++;
					}
					else
						return 3;
				}
				else
					return 3;
				break;

			default:
				return 3;
			}
		}
		else
			return 4;
	}

	for (i = 0; i <= n; i++)
	{
		for (int32_t j = set[i].first; j <= set[i].last; j++)
			(int64_t)* mask |= (1LL << (int64_t)j);
	}

	return 0;
}

uint32_t priority_mask()
{
	return REALTIME_PRIORITY_CLASS | HIGH_PRIORITY_CLASS | ABOVE_NORMAL_PRIORITY_CLASS | NORMAL_PRIORITY_CLASS | BELOW_NORMAL_PRIORITY_CLASS | IDLE_PRIORITY_CLASS;
}

int32_t priority_constant_to_index(uint32_t constant)
{
	switch (constant & priority_mask())
	{
	case REALTIME_PRIORITY_CLASS:
		return priority::realtime;
	case HIGH_PRIORITY_CLASS:
		return priority::high;
	case ABOVE_NORMAL_PRIORITY_CLASS:
		return priority::abovenormal;
	case BELOW_NORMAL_PRIORITY_CLASS:
		return priority::belownormal;
	case IDLE_PRIORITY_CLASS:
		return priority::idle;
	}
	return priority::normal;
}

uint32_t priority_index_to_constant(int32_t index)
{
	switch (index)
	{
	case priority::realtime:
		return REALTIME_PRIORITY_CLASS;
	case priority::high:
		return HIGH_PRIORITY_CLASS;
	case priority::abovenormal:
		return ABOVE_NORMAL_PRIORITY_CLASS;
	case priority::belownormal:
		return BELOW_NORMAL_PRIORITY_CLASS;
	case priority::idle:
		return IDLE_PRIORITY_CLASS;
	}
	return NORMAL_PRIORITY_CLASS;
}

static inline uint32_t throttle_milliseconds(uint32_t throttle)
{
	if (throttle > 7)
		throttle = 8;
	/* pow() operates on doubles. */
	uint32_t ret = 1;
	for (uint32_t i = 1; i < throttle; i++)
		ret *= 2;
	return ret * 1000;
}

void set_service_environment(nssm_service_t* service)
{
	if (!service)
		return;

	/*
    We have to duplicate the block because this function will be called
    multiple times between registry reads.
  */
	if (service->env)
		duplicate_environment_strings(service->env);
	if (!service->env_extra)
		return;
	wchar_t* env_extra = copy_environment_block(service->env_extra);
	if (!env_extra)
		return;

	set_environment_block(env_extra);
	HeapFree(GetProcessHeap(), 0, env_extra);
}

void unset_service_environment(nssm_service_t* service)
{
	if (!service)
		return;
	duplicate_environment_strings(service->initial_env);
}

/*
  Wrapper to be called in a new thread so that we can acknowledge a STOP
  control immediately.
*/
static uint32_t WINAPI shutdown_service(void* arg)
{
	return stop_service((nssm_service_t*)arg, 0, true, true);
}

/*
 Wrapper to be called in a new thread so that we can acknowledge start
 immediately.
*/
static uint32_t WINAPI launch_service(void* arg)
{
	return monitor_service((nssm_service_t*)arg);
}

/* Connect to the service manager */
SC_HANDLE open_service_manager(uint32_t access)
{
	SC_HANDLE ret = OpenSCManager(0, SERVICES_ACTIVE_DATABASE, access);
	if (!ret)
	{
		if (is_admin)
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENSCMANAGER_FAILED, 0);
		return 0;
	}

	return ret;
}

/* Open a service by name or display name. */
SC_HANDLE open_service(SC_HANDLE services, wchar_t* service_name, uint32_t access, wchar_t* canonical_name, uint32_t canonical_namelen)
{
	SC_HANDLE service_handle = OpenService(services, service_name, access);
	if (service_handle)
	{
		if (canonical_name && canonical_name != service_name)
		{
			wchar_t displayname[SERVICE_NAME_LENGTH];
			uint32_t displayname_len = (uint32_t)std::size(displayname);
			GetServiceDisplayName(services, service_name, displayname, &displayname_len);
			uint32_t keyname_len = canonical_namelen;
			GetServiceKeyName(services, displayname, canonical_name, &keyname_len);
		}
		return service_handle;
	}

	uint32_t error = GetLastError();
	if (error != ERROR_SERVICE_DOES_NOT_EXIST)
	{
		print_message(stderr, NSSM_MESSAGE_OPENSERVICE_FAILED, error_string(GetLastError()));
		return 0;
	}

	/* We can't look for a display name because there's no buffer to store it. */
	if (!canonical_name)
	{
		print_message(stderr, NSSM_MESSAGE_OPENSERVICE_FAILED, error_string(GetLastError()));
		return 0;
	}

	uint32_t bufsize, required, count, i;
	uint32_t resume = 0;
	EnumServicesStatusEx(services, SC_ENUM_PROCESS_INFO, SERVICE_DRIVER | SERVICE_FILE_SYSTEM_DRIVER | SERVICE_KERNEL_DRIVER | SERVICE_WIN32, SERVICE_STATE_ALL, 0, 0, &required, &count, &resume, 0);
	error = GetLastError();
	if (error != ERROR_MORE_DATA)
	{
		print_message(stderr, NSSM_MESSAGE_ENUMSERVICESSTATUS_FAILED, error_string(GetLastError()));
		return 0;
	}

	ENUM_SERVICE_STATUS_PROCESSW* status = (ENUM_SERVICE_STATUS_PROCESSW*)HeapAlloc(GetProcessHeap(), 0, required);
	if (!status)
	{
		print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"ENUM_SERVICE_STATUS_PROCESSW", L"open_service()");
		return 0;
	}

	bufsize = required;
	while (true)
	{
		/*
      EnumServicesStatusEx() returns:
      1 when it retrieved data and there's no more data to come.
      0 and sets last error to ERROR_MORE_DATA when it retrieved data and
        there's more data to come.
      0 and sets last error to something else on error.
    */
		int32_t ret = EnumServicesStatusEx(services, SC_ENUM_PROCESS_INFO, SERVICE_DRIVER | SERVICE_FILE_SYSTEM_DRIVER | SERVICE_KERNEL_DRIVER | SERVICE_WIN32, SERVICE_STATE_ALL, (uint8_t*)status, bufsize, &required, &count, &resume, 0);
		if (!ret)
		{
			error = GetLastError();
			if (error != ERROR_MORE_DATA)
			{
				HeapFree(GetProcessHeap(), 0, status);
				print_message(stderr, NSSM_MESSAGE_ENUMSERVICESSTATUS_FAILED, error_string(GetLastError()));
				return 0;
			}
		}

		for (i = 0; i < count; i++)
		{
			if (str_equiv(status[i].lpDisplayName, service_name))
			{
				if (::_snwprintf_s(canonical_name, canonical_namelen, _TRUNCATE, L"%s", status[i].lpServiceName) < 0)
				{
					HeapFree(GetProcessHeap(), 0, status);
					print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"canonical_name", L"open_service()");
					return 0;
				}

				HeapFree(GetProcessHeap(), 0, status);
				return open_service(services, canonical_name, access, 0, 0);
			}
		}

		if (ret)
			break;
	}

	/* Recurse so we can get an error message. */
	return open_service(services, service_name, access, 0, 0);
}

QUERY_SERVICE_CONFIGW* query_service_config(const wchar_t* service_name, SC_HANDLE service_handle)
{
	QUERY_SERVICE_CONFIGW* qsc;
	uint32_t bufsize;
	uint32_t error;

	QueryServiceConfig(service_handle, 0, 0, &bufsize);
	error = GetLastError();
	if (error == ERROR_INSUFFICIENT_BUFFER)
	{
		qsc = (QUERY_SERVICE_CONFIGW*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bufsize);
		if (!qsc)
		{
			print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"QUERY_SERVICE_CONFIGW", L"query_service_config()", 0);
			return 0;
		}
	}
	else
	{
		print_message(stderr, NSSM_MESSAGE_QUERYSERVICECONFIG_FAILED, service_name, error_string(error), 0);
		return 0;
	}

	if (!QueryServiceConfig(service_handle, qsc, bufsize, &bufsize))
	{
		HeapFree(GetProcessHeap(), 0, qsc);
		print_message(stderr, NSSM_MESSAGE_QUERYSERVICECONFIG_FAILED, service_name, error_string(GetLastError()), 0);
		return 0;
	}

	return qsc;
}

/* WILL NOT allocate a new string if the identifier is already present. */
int32_t prepend_service_group_identifier(wchar_t* group, wchar_t** canon)
{
	if (!group || !group[0] || group[0] == SC_GROUP_IDENTIFIER)
	{
		*canon = group;
		return 0;
	}

	size_t len = ::wcslen(group) + 1;
	*canon = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (len + 1) * sizeof(wchar_t));
	if (!*canon)
	{
		print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"canon", L"prepend_service_group_identifier()");
		return 1;
	}

	wchar_t* s = *canon;
	*s++ = SC_GROUP_IDENTIFIER;
	memmove(s, group, len * sizeof(wchar_t));
	(*canon)[len] = _T('\0');

	return 0;
}

int32_t append_to_dependencies(wchar_t* dependencies, uint32_t dependencieslen, wchar_t* string, wchar_t** newdependencies, uint32_t* newlen, int32_t type)
{
	*newlen = 0;

	wchar_t* canon = 0;
	if (type == dependency::groups)
	{
		if (prepend_service_group_identifier(string, &canon))
			return 1;
	}
	else
		canon = string;
	int32_t ret = append_to_double_null(dependencies, dependencieslen, newdependencies, newlen, canon, 0, false);
	if (canon && canon != string)
		HeapFree(GetProcessHeap(), 0, canon);

	return ret;
}

int32_t remove_from_dependencies(wchar_t* dependencies, uint32_t dependencieslen, wchar_t* string, wchar_t** newdependencies, uint32_t* newlen, int32_t type)
{
	*newlen = 0;

	wchar_t* canon = 0;
	if (type == dependency::groups)
	{
		if (prepend_service_group_identifier(string, &canon))
			return 1;
	}
	else
		canon = string;
	int32_t ret = remove_from_double_null(dependencies, dependencieslen, newdependencies, newlen, canon, 0, false);
	if (canon && canon != string)
		HeapFree(GetProcessHeap(), 0, canon);

	return ret;
}

int32_t set_service_dependencies(const wchar_t* service_name, SC_HANDLE service_handle, wchar_t* buffer)
{
	wchar_t* dependencies = L"";
	uint32_t num_dependencies = 0;

	if (buffer && buffer[0])
	{
		SC_HANDLE services = open_service_manager(SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
		if (!services)
		{
			print_message(stderr, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
			return 1;
		}

		/*
      Count the dependencies then allocate a buffer big enough for their
      canonical names, ie n * SERVICE_NAME_LENGTH.
    */
		wchar_t* s;
		wchar_t* groups = 0;
		for (s = buffer; *s; s++)
		{
			num_dependencies++;
			if (*s == SC_GROUP_IDENTIFIER)
				groups = s;
			while (*s)
				s++;
		}

		/* At least one dependency is a group so we need to verify them. */
		if (groups)
		{
			HKEY key;
			if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, regliterals::reggroupspath.data(), 0, KEY_READ, &key))
			{
				fwprintf(stderr, L"%s: %s\n", regliterals::reggroupspath.data(), error_string(GetLastError()));
				return 2;
			}

			uint32_t type;
			uint32_t groupslen;
			uint32_t ret = ::RegQueryValueExW(key, regliterals::reggroups.data(), 0, &type, nullptr, &groupslen);
			if (ret == ERROR_SUCCESS)
			{
				groups = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, groupslen);
				if (!groups)
				{
					print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"groups", L"set_service_dependencies()");
					return 3;
				}

				ret = ::RegQueryValueExW(key, regliterals::reggroups.data(), 0, &type, (uint8_t*)groups, &groupslen);
				if (ret != ERROR_SUCCESS)
				{
					fwprintf(stderr, L"%s\\%s: %s", regliterals::reggroupspath.data(), regliterals::reggroups.data(), error_string(GetLastError()));
					HeapFree(GetProcessHeap(), 0, groups);
					RegCloseKey(key);
					return 4;
				}
			}
			else if (ret != ERROR_FILE_NOT_FOUND)
			{
				fwprintf(stderr, L"%s\\%s: %s", regliterals::reggroupspath.data(), regliterals::reggroups.data(), error_string(GetLastError()));
				RegCloseKey(key);
				return 4;
			}

			RegCloseKey(key);
		}

		uint32_t dependencieslen = (num_dependencies * SERVICE_NAME_LENGTH) + 2;
		dependencies = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dependencieslen * sizeof(wchar_t));
		size_t i = 0;

		wchar_t dependency[SERVICE_NAME_LENGTH];
		for (s = buffer; *s; s++)
		{
			/* Group? */
			if (*s == SC_GROUP_IDENTIFIER)
			{
				wchar_t* group = s + 1;

				bool ok = false;
				if (*group)
				{
					for (wchar_t* g = groups; *g; g++)
					{
						if (str_equiv(g, group))
						{
							ok = true;
							/* Set canonical name. */
							memmove(group, g, ::wcslen(g) * sizeof(wchar_t));
							break;
						}

						while (*g)
							g++;
					}
				}

				if (ok)
					::_snwprintf_s(dependency, std::size(dependency), _TRUNCATE, L"%s", s);
				else
				{
					HeapFree(GetProcessHeap(), 0, dependencies);
					if (groups)
						HeapFree(GetProcessHeap(), 0, groups);
					fwprintf(stderr, L"%s: %s", s, error_string(ERROR_SERVICE_DEPENDENCY_DELETED));
					return 5;
				}
			}
			else
			{
				SC_HANDLE dependency_handle = open_service(services, s, SERVICE_QUERY_STATUS, dependency, std::size(dependency));
				if (!dependency_handle)
				{
					HeapFree(GetProcessHeap(), 0, dependencies);
					if (groups)
						HeapFree(GetProcessHeap(), 0, groups);
					CloseServiceHandle(services);
					fwprintf(stderr, L"%s: %s", s, error_string(ERROR_SERVICE_DEPENDENCY_DELETED));
					return 5;
				}
			}

			size_t len = ::wcslen(dependency) + 1;
			memmove(dependencies + i, dependency, len * sizeof(wchar_t));
			i += len;

			while (*s)
				s++;
		}

		if (groups)
			HeapFree(GetProcessHeap(), 0, groups);
		CloseServiceHandle(services);
	}

	if (!ChangeServiceConfig(service_handle, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, 0, 0, 0, dependencies, 0, 0, 0))
	{
		if (num_dependencies)
			HeapFree(GetProcessHeap(), 0, dependencies);
		print_message(stderr, NSSM_MESSAGE_CHANGESERVICECONFIG_FAILED, error_string(GetLastError()));
		return -1;
	}

	if (num_dependencies)
		HeapFree(GetProcessHeap(), 0, dependencies);
	return 0;
}

int32_t get_service_dependencies(const wchar_t* service_name, SC_HANDLE service_handle, wchar_t** buffer, uint32_t* bufsize, int32_t type)
{
	if (!buffer)
		return 1;
	if (!bufsize)
		return 2;

	*buffer = 0;
	*bufsize = 0;

	QUERY_SERVICE_CONFIGW* qsc = query_service_config(service_name, service_handle);
	if (!qsc)
		return 3;

	if (!qsc->lpDependencies || !qsc->lpDependencies[0])
	{
		HeapFree(GetProcessHeap(), 0, qsc);
		return 0;
	}

	/* lpDependencies is doubly nullptr terminated. */
	while (qsc->lpDependencies[*bufsize])
	{
		while (qsc->lpDependencies[*bufsize])
			++*bufsize;
		++*bufsize;
	}

	*bufsize += 2;

	*buffer = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, *bufsize * sizeof(wchar_t));
	if (!*buffer)
	{
		*bufsize = 0;
		print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"lpDependencies", L"get_service_dependencies()");
		HeapFree(GetProcessHeap(), 0, qsc);
		return 4;
	}

	if (type == dependency::all)
		memmove(*buffer, qsc->lpDependencies, *bufsize * sizeof(wchar_t));
	else
	{
		wchar_t* s;
		size_t i = 0;
		*bufsize = 0;
		for (s = qsc->lpDependencies; *s; s++)
		{
			/* Only copy the appropriate type of dependency. */
			if ((*s == SC_GROUP_IDENTIFIER && type & dependency::groups) || (*s != SC_GROUP_IDENTIFIER && type & dependency::services))
			{
				size_t len = ::wcslen(s) + 1;
				*bufsize += (uint32_t)len;
				memmove(*buffer + i, s, len * sizeof(wchar_t));
				i += len;
			}

			while (*s)
				s++;
		}
		++*bufsize;
	}

	HeapFree(GetProcessHeap(), 0, qsc);

	if (!*buffer[0])
	{
		HeapFree(GetProcessHeap(), 0, *buffer);
		*buffer = 0;
		*bufsize = 0;
	}

	return 0;
}

int32_t get_service_dependencies(const wchar_t* service_name, SC_HANDLE service_handle, wchar_t** buffer, uint32_t* bufsize)
{
	return get_service_dependencies(service_name, service_handle, buffer, bufsize, dependency::all);
}

int32_t set_service_description(const wchar_t* service_name, SC_HANDLE service_handle, wchar_t* buffer)
{
	SERVICE_DESCRIPTIONW description;
	ZeroMemory(&description, sizeof(description));
	/*
    lpDescription must be nullptr if we aren't changing, the new description
    or "".
  */
	if (buffer && buffer[0])
		description.lpDescription = buffer;
	else
		description.lpDescription = L"";

	if (ChangeServiceConfig2(service_handle, SERVICE_CONFIG_DESCRIPTION, &description))
		return 0;

	log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SERVICE_CONFIG_DESCRIPTION_FAILED, service_name, error_string(GetLastError()), 0);
	return 1;
}

int32_t get_service_description(const wchar_t* service_name, SC_HANDLE service_handle, uint32_t len, wchar_t* buffer)
{
	if (!buffer)
		return 1;

	uint32_t bufsize;
	QueryServiceConfig2(service_handle, SERVICE_CONFIG_DESCRIPTION, 0, 0, &bufsize);
	uint32_t error = GetLastError();
	if (error == ERROR_INSUFFICIENT_BUFFER)
	{
		SERVICE_DESCRIPTIONW* description = (SERVICE_DESCRIPTIONW*)HeapAlloc(GetProcessHeap(), 0, bufsize);
		if (!description)
		{
			print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"SERVICE_CONFIG_DESCRIPTION", L"get_service_description()");
			return 2;
		}

		if (QueryServiceConfig2(service_handle, SERVICE_CONFIG_DESCRIPTION, (uint8_t*)description, bufsize, &bufsize))
		{
			if (description->lpDescription)
				::_snwprintf_s(buffer, len, _TRUNCATE, L"%s", description->lpDescription);
			else
				ZeroMemory(buffer, len * sizeof(wchar_t));
			HeapFree(GetProcessHeap(), 0, description);
			return 0;
		}
		else
		{
			HeapFree(GetProcessHeap(), 0, description);
			print_message(stderr, NSSM_MESSAGE_QUERYSERVICECONFIG2_FAILED, service_name, L"SERVICE_CONFIG_DESCRIPTION", error_string(error));
			return 3;
		}
	}
	else
	{
		print_message(stderr, NSSM_MESSAGE_QUERYSERVICECONFIG2_FAILED, service_name, L"SERVICE_CONFIG_DESCRIPTION", error_string(error));
		return 4;
	}
}

int32_t get_service_startup(const wchar_t* service_name, SC_HANDLE service_handle, const QUERY_SERVICE_CONFIGW* qsc, uint32_t* startup)
{
	if (!qsc)
		return 1;

	switch (qsc->dwStartType)
	{
	case SERVICE_DEMAND_START:
		*startup = startup::manual;
		break;
	case SERVICE_DISABLED:
		*startup = startup::disabled;
		break;
	default:
		*startup = startup::automatic;
	}

	if (*startup != startup::automatic)
		return 0;

	/* Check for delayed start. */
	uint32_t bufsize;
	uint32_t error;
	QueryServiceConfig2(service_handle, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, 0, 0, &bufsize);
	error = GetLastError();
	if (error == ERROR_INSUFFICIENT_BUFFER)
	{
		SERVICE_DELAYED_AUTO_START_INFO* info = (SERVICE_DELAYED_AUTO_START_INFO*)HeapAlloc(GetProcessHeap(), 0, bufsize);
		if (!info)
		{
			print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"SERVICE_DELAYED_AUTO_START_INFO", L"get_service_startup()");
			return 2;
		}

		if (QueryServiceConfig2(service_handle, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, (uint8_t*)info, bufsize, &bufsize))
		{
			if (info->fDelayedAutostart)
				*startup = startup::delayed;
			HeapFree(GetProcessHeap(), 0, info);
			return 0;
		}
		else
		{
			error = GetLastError();
			if (error != ERROR_INVALID_LEVEL)
			{
				print_message(stderr, NSSM_MESSAGE_QUERYSERVICECONFIG2_FAILED, service_name, L"SERVICE_CONFIG_DELAYED_AUTO_START_INFO", error_string(error));
				return 3;
			}
		}
	}
	else if (error != ERROR_INVALID_LEVEL)
	{
		print_message(stderr, NSSM_MESSAGE_QUERYSERVICECONFIG2_FAILED, service_name, L"SERVICE_DELAYED_AUTO_START_INFO", error_string(error));
		return 3;
	}

	return 0;
}

int32_t get_service_username(const wchar_t* service_name, const QUERY_SERVICE_CONFIGW* qsc, wchar_t** username, size_t* usernamelen)
{
	if (!username)
		return 1;
	if (!usernamelen)
		return 1;

	*username = 0;
	*usernamelen = 0;

	if (!qsc)
		return 1;

	if (qsc->lpServiceStartName[0])
	{
		if (is_localsystem(qsc->lpServiceStartName))
			return 0;

		size_t len = ::wcslen(qsc->lpServiceStartName);
		*username = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (len + 1) * sizeof(wchar_t));
		if (!*username)
		{
			print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"username", L"get_service_username()");
			return 2;
		}

		memmove(*username, qsc->lpServiceStartName, (len + 1) * sizeof(wchar_t));
		*usernamelen = len;
	}

	return 0;
}

/* Set default values which aren't zero. */
void set_nssm_service_defaults(nssm_service_t* service)
{
	if (!service)
		return;

	service->type = SERVICE_WIN32_OWN_PROCESS;
	service->priority = NORMAL_PRIORITY_CLASS;
	service->stdin_sharing = NSSM_STDIN_SHARING;
	service->stdin_disposition = NSSM_STDIN_DISPOSITION;
	service->stdin_flags = NSSM_STDIN_FLAGS;
	service->stdout_sharing = NSSM_STDOUT_SHARING;
	service->stdout_disposition = NSSM_STDOUT_DISPOSITION;
	service->stdout_flags = NSSM_STDOUT_FLAGS;
	service->stderr_sharing = NSSM_STDERR_SHARING;
	service->stderr_disposition = NSSM_STDERR_DISPOSITION;
	service->stderr_flags = NSSM_STDERR_FLAGS;
	service->throttle_delay = wait::reset_throttle_restart;
	service->stop_method = ~0;
	service->kill_console_delay = wait::kill_console_grace_period;
	service->kill_window_delay = wait::kill_window_grace_period;
	service->kill_threads_delay = wait::kill_threads_grace_period;
	service->kill_process_tree = 1;
}

/* Allocate and zero memory for a service. */
nssm_service_t* alloc_nssm_service()
{
	nssm_service_t* service = (nssm_service_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(nssm_service_t));
	if (!service)
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"service", L"alloc_nssm_service()", 0);
	return service;
}

/* Free memory for a service. */
void cleanup_nssm_service(nssm_service_t* service)
{
	if (!service)
		return;
	if (service->username)
		HeapFree(GetProcessHeap(), 0, service->username);
	if (service->password)
	{
		SecureZeroMemory(service->password, service->passwordlen * sizeof(wchar_t));
		HeapFree(GetProcessHeap(), 0, service->password);
	}
	if (service->dependencies)
		HeapFree(GetProcessHeap(), 0, service->dependencies);
	if (service->env)
		HeapFree(GetProcessHeap(), 0, service->env);
	if (service->env_extra)
		HeapFree(GetProcessHeap(), 0, service->env_extra);
	if (service->handle)
		CloseServiceHandle(service->handle);
	if (service->process_handle)
		CloseHandle(service->process_handle);
	if (service->wait_handle)
		UnregisterWait(service->wait_handle);
	if (service->throttle_section_initialised)
		DeleteCriticalSection(&service->throttle_section);
	if (service->throttle_timer)
		CloseHandle(service->throttle_timer);
	if (service->hook_section_initialised)
		DeleteCriticalSection(&service->hook_section);
	if (service->initial_env)
		HeapFree(GetProcessHeap(), 0, service->initial_env);
	HeapFree(GetProcessHeap(), 0, service);
}

/* About to install the service */
int32_t pre_install_service(int32_t argc, wchar_t** argv)
{
	nssm_service_t* service = alloc_nssm_service();
	set_nssm_service_defaults(service);
	if (argc)
		::_snwprintf_s(service->name, std::size(service->name), _TRUNCATE, L"%s", argv[0]);

	/* Show the dialogue box if we didn't give the service name and path */
	if (argc < 2)
		return nssm_gui(IDD_INSTALL, service);

	if (!service)
	{
		print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"service", L"pre_install_service()");
		return 1;
	}
	::_snwprintf_s(service->exe, std::size(service->exe), _TRUNCATE, L"%s", argv[1]);

	/* Arguments are optional */
	size_t flagslen = 0;
	size_t s = 0;
	int32_t i;
	for (i = 2; i < argc; i++)
		flagslen += ::wcslen(argv[i]) + 1;
	if (!flagslen)
		flagslen = 1;
	if (flagslen > std::size(service->flags))
	{
		print_message(stderr, NSSM_MESSAGE_FLAGS_TOO_LONG);
		return 2;
	}

	for (i = 2; i < argc; i++)
	{
		size_t len = ::wcslen(argv[i]);
		memmove(service->flags + s, argv[i], len * sizeof(wchar_t));
		s += len;
		if (i < argc - 1)
			service->flags[s++] = _T(' ');
	}

	/* Work out directory name */
	::_snwprintf_s(service->dir, std::size(service->dir), _TRUNCATE, L"%s", service->exe);
	strip_basename(service->dir);

	int32_t ret = install_service(service);
	cleanup_nssm_service(service);
	return ret;
}

/* About to edit the service. */
int32_t pre_edit_service(int32_t argc, wchar_t** argv)
{
	/* Require service name. */
	if (argc < 2)
		return usage(1);

	/* Are we editing on the command line? */
	enum
	{
		MODE_EDITING,
		MODE_GETTING,
		MODE_SETTING,
		MODE_RESETTING,
		MODE_DUMPING
	} mode = MODE_EDITING;
	const wchar_t* verb = argv[0];
	const wchar_t* service_name = argv[1];
	bool getting = false;
	bool unsetting = false;

	/* Minimum number of arguments. */
	int32_t mandatory = 2;
	/* Index of first value. */
	int32_t remainder = 3;
	int32_t i;
	if (str_equiv(verb, L"get"))
	{
		mandatory = 3;
		mode = MODE_GETTING;
	}
	else if (str_equiv(verb, L"set"))
	{
		mandatory = 4;
		mode = MODE_SETTING;
	}
	else if (str_equiv(verb, L"reset") || str_equiv(verb, L"unset"))
	{
		mandatory = 3;
		mode = MODE_RESETTING;
	}
	else if (str_equiv(verb, L"dump"))
	{
		mandatory = 1;
		remainder = 2;
		mode = MODE_DUMPING;
	}
	if (argc < mandatory)
		return usage(1);

	const wchar_t* parameter = 0;
	settings_t* setting = 0;
	wchar_t* additional;

	/* Validate the parameter. */
	if (mandatory > 2)
	{
		bool additional_mandatory = false;

		parameter = argv[2];
		for (i = 0; settings[i].name; i++)
		{
			setting = &settings[i];
			if (!str_equiv(setting->name, parameter))
				continue;
			if (((setting->additional & additionalarg::getting) && mode == MODE_GETTING) || ((setting->additional & additionalarg::setting) && mode == MODE_SETTING) || ((setting->additional & additionalarg::resetting) && mode == MODE_RESETTING))
			{
				additional_mandatory = true;
				mandatory++;
			}
			break;
		}
		if (!settings[i].name)
		{
			print_message(stderr, NSSM_MESSAGE_INVALID_PARAMETER, parameter);
			for (i = 0; settings[i].name; i++)
				fwprintf(stderr, L"%s\n", settings[i].name);
			return 1;
		}

		additional = 0;
		if (additional_mandatory)
		{
			if (argc < mandatory)
			{
				print_message(stderr, NSSM_MESSAGE_MISSING_SUBPARAMETER, parameter);
				return 1;
			}
			additional = argv[3];
			remainder = 4;
		}
		else if (str_equiv(setting->name, nativeliterals::objectname.data()) && mode == MODE_SETTING)
		{
			additional = argv[3];
			remainder = 4;
		}
		else
		{
			additional = argv[remainder];
			if (argc < mandatory)
				return usage(1);
		}
	}

	nssm_service_t* service = alloc_nssm_service();
	::_snwprintf_s(service->name, std::size(service->name), _TRUNCATE, L"%s", service_name);

	/* Open service manager */
	SC_HANDLE services = open_service_manager(SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
	if (!services)
	{
		print_message(stderr, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
		return 2;
	}

	/* Try to open the service */
	uint32_t access = SERVICE_QUERY_CONFIG;
	if (mode != MODE_GETTING)
		access |= SERVICE_CHANGE_CONFIG;
	service->handle = open_service(services, service->name, access, service->name, std::size(service->name));
	if (!service->handle)
	{
		CloseServiceHandle(services);
		return 3;
	}

	/* Get system details. */
	QUERY_SERVICE_CONFIGW* qsc = query_service_config(service->name, service->handle);
	if (!qsc)
	{
		CloseServiceHandle(service->handle);
		CloseServiceHandle(services);
		return 4;
	}

	service->type = qsc->dwServiceType;
	if (!(service->type & SERVICE_WIN32_OWN_PROCESS))
	{
		if (mode != MODE_GETTING && mode != MODE_DUMPING)
		{
			HeapFree(GetProcessHeap(), 0, qsc);
			CloseServiceHandle(service->handle);
			CloseServiceHandle(services);
			print_message(stderr, NSSM_MESSAGE_CANNOT_EDIT, service->name, NSSM_WIN32_OWN_PROCESS, 0);
			return 3;
		}
	}

	if (get_service_startup(service->name, service->handle, qsc, &service->startup))
	{
		if (mode != MODE_GETTING && mode != MODE_DUMPING)
		{
			HeapFree(GetProcessHeap(), 0, qsc);
			CloseServiceHandle(service->handle);
			CloseServiceHandle(services);
			return 4;
		}
	}

	if (get_service_username(service->name, qsc, &service->username, &service->usernamelen))
	{
		if (mode != MODE_GETTING && mode != MODE_DUMPING)
		{
			HeapFree(GetProcessHeap(), 0, qsc);
			CloseServiceHandle(service->handle);
			CloseServiceHandle(services);
			return 5;
		}
	}

	::_snwprintf_s(service->displayname, std::size(service->displayname), _TRUNCATE, L"%s", qsc->lpDisplayName);

	/* Get the canonical service name. We open it case insensitively. */
	uint32_t bufsize = std::size(service->name);
	GetServiceKeyName(services, service->displayname, service->name, &bufsize);

	/* Remember the executable in case it isn't NSSM. */
	::_snwprintf_s(service->image, std::size(service->image), _TRUNCATE, L"%s", qsc->lpBinaryPathName);
	HeapFree(GetProcessHeap(), 0, qsc);

	/* Get extended system details. */
	if (get_service_description(service->name, service->handle, std::size(service->description), service->description))
	{
		if (mode != MODE_GETTING && mode != MODE_DUMPING)
		{
			CloseServiceHandle(service->handle);
			CloseServiceHandle(services);
			return 6;
		}
	}

	if (get_service_dependencies(service->name, service->handle, &service->dependencies, &service->dependencieslen))
	{
		if (mode != MODE_GETTING && mode != MODE_DUMPING)
		{
			CloseServiceHandle(service->handle);
			CloseServiceHandle(services);
			return 7;
		}
	}

	/* Get NSSM details. */
	get_parameters(service, 0);

	CloseServiceHandle(services);

	if (!service->exe[0])
	{
		service->native = true;
		if (mode != MODE_GETTING && mode != MODE_DUMPING)
			print_message(stderr, NSSM_MESSAGE_INVALID_SERVICE, service->name, NSSM, service->image);
	}

	/* Editing with the GUI. */
	if (mode == MODE_EDITING)
	{
		nssm_gui(IDD_EDIT, service);
		return 0;
	}

	HKEY key;
	value_t value;
	int32_t ret;

	if (mode == MODE_DUMPING)
	{
		wchar_t* service_name = service->name;
		if (argc > remainder)
			service_name = argv[remainder];
		if (service->native)
			key = 0;
		else
		{
			key = open_registry(service->name, KEY_READ);
			if (!key)
				return 4;
		}

		wchar_t quoted_service_name[SERVICE_NAME_LENGTH * 2];
		wchar_t quoted_exe[EXE_LENGTH * 2];
		wchar_t quoted_nssm[EXE_LENGTH * 2];
		if (quote(service_name, quoted_service_name, std::size(quoted_service_name)))
			return 5;
		if (quote(service->exe, quoted_exe, std::size(quoted_exe)))
			return 6;
		if (quote(nssm_exe(), quoted_nssm, std::size(quoted_nssm)))
			return 6;
		wprintf(L"%s install %s %s\n", quoted_nssm, quoted_service_name, quoted_exe);

		ret = 0;
		for (i = 0; settings[i].name; i++)
		{
			setting = &settings[i];
			if (!setting->native && service->native)
				continue;
			if (dump_setting(service_name, key, service->handle, setting))
				ret++;
		}

		if (!service->native)
			RegCloseKey(key);
		CloseServiceHandle(service->handle);

		if (ret)
			return 1;
		return 0;
	}

	/* Trying to manage App* parameters for a non-NSSM service. */
	if (!setting->native && service->native)
	{
		CloseServiceHandle(service->handle);
		print_message(stderr, NSSM_MESSAGE_NATIVE_PARAMETER, setting->name, NSSM);
		return 1;
	}

	if (mode == MODE_GETTING)
	{
		if (!service->native)
		{
			key = open_registry(service->name, KEY_READ);
			if (!key)
				return 4;
		}

		if (setting->native)
			ret = get_setting(service->name, service->handle, setting, &value, additional);
		else
			ret = get_setting(service->name, key, setting, &value, additional);
		if (ret < 0)
		{
			CloseServiceHandle(service->handle);
			return 5;
		}

		switch (setting->type)
		{
		case REG_EXPAND_SZ:
		case REG_MULTI_SZ:
		case REG_SZ:
			wprintf(L"%s\n", value.string ? value.string : L"");
			HeapFree(GetProcessHeap(), 0, value.string);
			break;

		case REG_DWORD:
			wprintf(L"%u\n", value.numeric);
			break;
		}

		if (!service->native)
			RegCloseKey(key);
		CloseServiceHandle(service->handle);
		return 0;
	}

	/* Build the value. */
	if (mode == MODE_RESETTING)
	{
		/* Unset the parameter. */
		value.string = 0;
	}
	else if (remainder == argc)
	{
		value.string = 0;
	}
	else
	{
		/* Set the parameter. */
		size_t len = 0;
		size_t delimiterlen = (setting->additional & additionalarg::crlf) ? 2 : 1;
		for (i = remainder; i < argc; i++)
			len += ::wcslen(argv[i]) + delimiterlen;
		len++;

		value.string = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, len * sizeof(wchar_t));
		if (!value.string)
		{
			print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"value", L"edit_service()");
			CloseServiceHandle(service->handle);
			return 2;
		}

		size_t s = 0;
		for (i = remainder; i < argc; i++)
		{
			size_t len = ::wcslen(argv[i]);
			memmove(value.string + s, argv[i], len * sizeof(wchar_t));
			s += len;
			if (i < argc - 1)
			{
				if (setting->additional & additionalarg::crlf)
				{
					value.string[s++] = _T('\r');
					value.string[s++] = _T('\n');
				}
				else
					value.string[s++] = _T(' ');
			}
		}
		value.string[s] = _T('\0');
	}

	if (!service->native)
	{
		key = open_registry(service->name, KEY_READ | KEY_WRITE);
		if (!key)
		{
			if (value.string)
				HeapFree(GetProcessHeap(), 0, value.string);
			return 4;
		}
	}

	if (setting->native)
		ret = set_setting(service->name, service->handle, setting, &value, additional);
	else
		ret = set_setting(service->name, key, setting, &value, additional);
	if (value.string)
		HeapFree(GetProcessHeap(), 0, value.string);
	if (ret < 0)
	{
		if (!service->native)
			RegCloseKey(key);
		CloseServiceHandle(service->handle);
		return 6;
	}

	if (!service->native)
		RegCloseKey(key);
	CloseServiceHandle(service->handle);

	return 0;
}

/* About to remove the service */
int32_t pre_remove_service(int32_t argc, wchar_t** argv)
{
	nssm_service_t* service = alloc_nssm_service();
	set_nssm_service_defaults(service);
	if (argc)
		::_snwprintf_s(service->name, std::size(service->name), _TRUNCATE, L"%s", argv[0]);

	/* Show dialogue box if we didn't pass service name and "confirm" */
	if (argc < 2)
		return nssm_gui(IDD_REMOVE, service);
	if (str_equiv(argv[1], L"confirm"))
	{
		int32_t ret = remove_service(service);
		cleanup_nssm_service(service);
		return ret;
	}
	print_message(stderr, NSSM_MESSAGE_PRE_REMOVE_SERVICE);
	return 100;
}

/* Install the service */
int32_t install_service(nssm_service_t* service)
{
	if (!service)
		return 1;

	/* Open service manager */
	SC_HANDLE services = open_service_manager(SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
	if (!services)
	{
		print_message(stderr, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
		cleanup_nssm_service(service);
		return 2;
	}

	/* Get path of this program */
	::_snwprintf_s(service->image, std::size(service->image), _TRUNCATE, L"%s", nssm_imagepath());

	/* Create the service - settings will be changed in edit_service() */
	service->handle = CreateService(services, service->name, service->name, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, service->image, 0, 0, 0, 0, 0);
	if (!service->handle)
	{
		print_message(stderr, NSSM_MESSAGE_CREATESERVICE_FAILED, error_string(GetLastError()));
		CloseServiceHandle(services);
		return 5;
	}

	if (edit_service(service, false))
	{
		DeleteService(service->handle);
		CloseServiceHandle(services);
		return 6;
	}

	print_message(stdout, NSSM_MESSAGE_SERVICE_INSTALLED, service->name);

	/* Cleanup */
	CloseServiceHandle(services);

	return 0;
}

/* Edit the service. */
int32_t edit_service(nssm_service_t* service, bool editing)
{
	if (!service)
		return 1;

	/*
    The only two valid flags for service type are SERVICE_WIN32_OWN_PROCESS
    and SERVICE_INTERACTIVE_PROCESS.
  */
	service->type &= SERVICE_INTERACTIVE_PROCESS;
	service->type |= SERVICE_WIN32_OWN_PROCESS;

	/* Startup type. */
	uint32_t startup;
	switch (service->startup)
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

	/* Display name. */
	if (!service->displayname[0])
		::_snwprintf_s(service->displayname, std::size(service->displayname), _TRUNCATE, L"%s", service->name);

	/*
    Username must be nullptr if we aren't changing or an account name.
    We must explicitly use LOCALSYSTEM to change it when we are editing.
    Password must be nullptr if we aren't changing, a password or "".
    Empty passwords are valid but we won't allow them in the GUI.
  */
	wchar_t* username = 0;
	wchar_t* canon = 0;
	wchar_t* password = 0;
	boolean virtual_account = false;
	if (service->usernamelen)
	{
		username = service->username;
		if (is_virtual_account(service->name, username))
		{
			virtual_account = true;
			canon = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (service->usernamelen + 1) * sizeof(wchar_t));
			if (!canon)
			{
				print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"canon", L"edit_service()");
				return 5;
			}
			memmove(canon, username, (service->usernamelen + 1) * sizeof(wchar_t));
		}
		else
		{
			if (canonicalise_username(username, &canon))
				return 5;
			if (service->passwordlen)
				password = service->password;
		}
	}
	else if (editing)
		username = canon = NSSM_LOCALSYSTEM_ACCOUNT;

	if (!virtual_account)
	{
		if (well_known_username(canon))
			password = L"";
		else
		{
			if (grant_logon_as_service(canon))
			{
				if (canon != username)
					HeapFree(GetProcessHeap(), 0, canon);
				print_message(stderr, NSSM_MESSAGE_GRANT_LOGON_AS_SERVICE_FAILED, username);
				return 5;
			}
		}
	}

	wchar_t* dependencies = L"";
	if (service->dependencieslen)
		dependencies = 0; /* Change later. */

	if (!ChangeServiceConfig(service->handle, service->type, startup, SERVICE_NO_CHANGE, 0, 0, 0, dependencies, canon, password, service->displayname))
	{
		if (canon != username)
			HeapFree(GetProcessHeap(), 0, canon);
		print_message(stderr, NSSM_MESSAGE_CHANGESERVICECONFIG_FAILED, error_string(GetLastError()));
		return 5;
	}
	if (canon != username)
		HeapFree(GetProcessHeap(), 0, canon);

	if (service->dependencieslen)
	{
		if (set_service_dependencies(service->name, service->handle, service->dependencies))
			return 5;
	}

	if (service->description[0] || editing)
	{
		set_service_description(service->name, service->handle, service->description);
	}

	SERVICE_DELAYED_AUTO_START_INFO delayed;
	ZeroMemory(&delayed, sizeof(delayed));
	if (service->startup == startup::delayed)
		delayed.fDelayedAutostart = 1;
	else
		delayed.fDelayedAutostart = 0;
	/* Delayed startup isn't supported until Vista. */
	if (!ChangeServiceConfig2(service->handle, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &delayed))
	{
		uint32_t error = GetLastError();
		/* Pre-Vista we expect to fail with ERROR_INVALID_LEVEL */
		if (error != ERROR_INVALID_LEVEL)
		{
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SERVICE_CONFIG_DELAYED_AUTO_START_INFO_FAILED, service->name, error_string(error), 0);
		}
	}

	/* Don't mess with parameters which aren't ours. */
	if (!service->native)
	{
		/* Now we need to put the parameters into the registry */
		if (create_parameters(service, editing))
		{
			print_message(stderr, NSSM_MESSAGE_CREATE_PARAMETERS_FAILED);
			return 6;
		}

		set_service_recovery(service);
	}

	return 0;
}

/* Control a service. */
int32_t control_service(uint32_t control, int32_t argc, wchar_t** argv, bool return_status)
{
	if (argc < 1)
		return usage(1);
	wchar_t* service_name = argv[0];
	wchar_t canonical_name[SERVICE_NAME_LENGTH];

	SC_HANDLE services = open_service_manager(SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
	if (!services)
	{
		print_message(stderr, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
		if (return_status)
			return 0;
		return 2;
	}

	uint32_t access = SERVICE_QUERY_STATUS;
	switch (control)
	{
	case wait::controlstart:
		access |= SERVICE_START;
		break;

	case SERVICE_CONTROL_CONTINUE:
	case SERVICE_CONTROL_PAUSE:
		access |= SERVICE_PAUSE_CONTINUE;
		break;

	case SERVICE_CONTROL_STOP:
		access |= SERVICE_STOP;
		break;

	case wait::controlrotate:
		access |= SERVICE_USER_DEFINED_CONTROL;
		break;
	}

	SC_HANDLE service_handle = open_service(services, service_name, access, canonical_name, std::size(canonical_name));
	if (!service_handle)
	{
		CloseServiceHandle(services);
		if (return_status)
			return 0;
		return 3;
	}

	int32_t ret;
	uint32_t error;
	SERVICE_STATUS service_status;
	if (control == wait::controlstart)
	{
		uint32_t initial_status = SERVICE_STOPPED;
		ret = StartService(service_handle, (uint32_t)argc, (const wchar_t**)argv);
		error = GetLastError();
		CloseServiceHandle(services);

		if (error == ERROR_IO_PENDING)
		{
			/*
        Older versions of Windows return immediately with ERROR_IO_PENDING
        indicate that the operation is still in progress.  Newer versions
        will return it if there really is a delay.
      */
			ret = 1;
			error = ERROR_SUCCESS;
		}

		if (ret)
		{
			uint32_t cutoff = 0;

			/* If we manage the service, respect the throttle time. */
			HKEY key = open_registry(service_name, 0, KEY_READ, false);
			if (key)
			{
				if (get_number(key, regliterals::regthrottle, &cutoff, false) != 1)
					cutoff = wait::reset_throttle_restart;
				RegCloseKey(key);
			}

			int32_t response = await_service_control_response(control, service_handle, &service_status, initial_status, cutoff);
			CloseServiceHandle(service_handle);

			if (response)
			{
				print_message(stderr, NSSM_MESSAGE_BAD_CONTROL_RESPONSE, canonical_name, service_status_text(service_status.dwCurrentState), service_control_text(control));
				if (return_status)
					return 0;
				return 1;
			}
			else
				wprintf(L"%s: %s: %s", canonical_name, service_control_text(control), error_string(error));
			return 0;
		}
		else
		{
			CloseServiceHandle(service_handle);
			fwprintf(stderr, L"%s: %s: %s", canonical_name, service_control_text(control), error_string(error));
			if (return_status)
				return 0;
			return 1;
		}
	}
	else if (control == SERVICE_CONTROL_INTERROGATE)
	{
		/*
      We could actually send an INTERROGATE control but that won't return
      any information if the service is stopped and we don't care about
      the extra details it might give us in any case.  So we'll fake it.
    */
		ret = QueryServiceStatus(service_handle, &service_status);
		error = GetLastError();

		if (ret)
		{
			wprintf(L"%s\n", service_status_text(service_status.dwCurrentState));
			if (return_status)
				return service_status.dwCurrentState;
			return 0;
		}
		else
		{
			fwprintf(stderr, L"%s: %s\n", canonical_name, error_string(error));
			if (return_status)
				return 0;
			return 1;
		}
	}
	else
	{
		ret = ControlService(service_handle, control, &service_status);
		uint32_t initial_status = service_status.dwCurrentState;
		error = GetLastError();
		CloseServiceHandle(services);

		if (error == ERROR_IO_PENDING)
		{
			ret = 1;
			error = ERROR_SUCCESS;
		}

		if (ret)
		{
			int32_t response = await_service_control_response(control, service_handle, &service_status, initial_status);
			CloseServiceHandle(service_handle);

			if (response)
			{
				print_message(stderr, NSSM_MESSAGE_BAD_CONTROL_RESPONSE, canonical_name, service_status_text(service_status.dwCurrentState), service_control_text(control));
				if (return_status)
					return 0;
				return 1;
			}
			else
				wprintf(L"%s: %s: %s", canonical_name, service_control_text(control), error_string(error));
			if (return_status)
				return service_status.dwCurrentState;
			return 0;
		}
		else
		{
			CloseServiceHandle(service_handle);
			fwprintf(stderr, L"%s: %s: %s", canonical_name, service_control_text(control), error_string(error));
			if (error == ERROR_SERVICE_NOT_ACTIVE)
			{
				if (control == SERVICE_CONTROL_SHUTDOWN || control == SERVICE_CONTROL_STOP)
				{
					if (return_status)
						return SERVICE_STOPPED;
					return 0;
				}
			}
			if (return_status)
				return 0;
			return 1;
		}
	}
}

int32_t control_service(uint32_t control, int32_t argc, wchar_t** argv)
{
	return control_service(control, argc, argv, false);
}

/* Remove the service */
int32_t remove_service(nssm_service_t* service)
{
	if (!service)
		return 1;

	/* Open service manager */
	SC_HANDLE services = open_service_manager(SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
	if (!services)
	{
		print_message(stderr, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
		return 2;
	}

	/* Try to open the service */
	service->handle = open_service(services, service->name, DELETE, service->name, std::size(service->name));
	if (!service->handle)
	{
		CloseServiceHandle(services);
		return 3;
	}

	/* Get the canonical service name. We open it case insensitively. */
	uint32_t bufsize = std::size(service->displayname);
	GetServiceDisplayName(services, service->name, service->displayname, &bufsize);
	bufsize = std::size(service->name);
	GetServiceKeyName(services, service->displayname, service->name, &bufsize);

	/* Try to delete the service */
	if (!DeleteService(service->handle))
	{
		print_message(stderr, NSSM_MESSAGE_DELETESERVICE_FAILED);
		CloseServiceHandle(services);
		return 4;
	}

	/* Cleanup */
	CloseServiceHandle(services);

	print_message(stdout, NSSM_MESSAGE_SERVICE_REMOVED, service->name);
	return 0;
}

/* Service initialisation */
void WINAPI service_main(uint32_t argc, wchar_t** argv)
{
	nssm_service_t* service = alloc_nssm_service();
	if (!service)
		return;

	static volatile bool await_debugger = (argc > 1 && str_equiv(argv[1], L"debug"));
	while (await_debugger)
		Sleep(1000);

	if (::_snwprintf_s(service->name, std::size(service->name), _TRUNCATE, L"%s", argv[0]) < 0)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"service->name", L"service_main()", 0);
		return;
	}

	/* We can use a condition variable in a critical section on Vista or later. */
	if (imports.SleepConditionVariableCS && imports.WakeConditionVariable)
		use_critical_section = true;
	else
		use_critical_section = false;

	/* Initialise status */
	ZeroMemory(&service->status, sizeof(service->status));
	service->status.dwServiceType = SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS;
	service->status.dwControlsAccepted = 0;
	service->status.dwWin32ExitCode = NO_ERROR;
	service->status.dwServiceSpecificExitCode = 0;
	service->status.dwCheckPoint = 0;
	service->status.dwWaitHint = wait::waithintmargin;

	/* Signal we AREN'T running the server */
	service->process_handle = 0;
	service->pid = 0;

	/* Register control handler */
	service->status_handle = RegisterServiceCtrlHandlerEx(NSSM, service_control_handler, (void*)service);
	if (!service->status_handle)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_REGISTERSERVICECTRLHANDER_FAILED, error_string(GetLastError()), 0);
		return;
	}

	log_service_control(service->name, 0, true);

	service->status.dwCurrentState = SERVICE_START_PENDING;
	service->status.dwWaitHint = service->throttle_delay + wait::waithintmargin;
	SetServiceStatus(service->status_handle, &service->status);

	if (is_admin)
	{
		/* Try to create the exit action parameters; we don't care if it fails */
		create_exit_action(service->name, exit_action_strings[0], false);

		SC_HANDLE services = open_service_manager(SC_MANAGER_CONNECT);
		if (services)
		{
			service->handle = open_service(services, service->name, SERVICE_CHANGE_CONFIG, 0, 0);
			set_service_recovery(service);

			/* Remember our display name. */
			uint32_t displayname_len = std::size(service->displayname);
			GetServiceDisplayName(services, service->name, service->displayname, &displayname_len);

			CloseServiceHandle(services);
		}
	}

	/* Used for signalling a resume if the service pauses when throttled. */
	if (use_critical_section)
	{
		InitializeCriticalSection(&service->throttle_section);
		service->throttle_section_initialised = true;
	}
	else
	{
		service->throttle_timer = CreateWaitableTimer(0, 1, 0);
		if (!service->throttle_timer)
		{
			log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_CREATEWAITABLETIMER_FAILED, service->name, error_string(GetLastError()), 0);
		}
	}

	/* Critical section for hooks. */
	InitializeCriticalSection(&service->hook_section);
	service->hook_section_initialised = true;

	/* Remember our initial environment. */
	service->initial_env = copy_environment();

	/* Remember our creation time. */
	if (get_process_creation_time(GetCurrentProcess(), &service->nssm_creation_time))
		ZeroMemory(&service->nssm_creation_time, sizeof(service->nssm_creation_time));

	service->allow_restart = true;
	if (!CreateThread(nullptr, 0, launch_service, (void*)service, 0, nullptr))
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATETHREAD_FAILED, error_string(GetLastError()), 0);
		stop_service(service, 0, true, true);
	}
}

/* Make sure service recovery actions are taken where necessary */
void set_service_recovery(nssm_service_t* service)
{
	SERVICE_FAILURE_ACTIONS_FLAG flag;
	ZeroMemory(&flag, sizeof(flag));
	flag.fFailureActionsOnNonCrashFailures = true;

	/* This functionality was added in Vista so the call may fail */
	if (!ChangeServiceConfig2(service->handle, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &flag))
	{
		uint32_t error = GetLastError();
		/* Pre-Vista we expect to fail with ERROR_INVALID_LEVEL */
		if (error != ERROR_INVALID_LEVEL)
		{
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SERVICE_CONFIG_FAILURE_ACTIONS_FAILED, service->name, error_string(error), 0);
		}
	}
}

int32_t monitor_service(nssm_service_t* service)
{
	/* Set service status to started */
	int32_t ret = start_service(service);
	if (ret)
	{
		wchar_t code[16];
		::_snwprintf_s(code, std::size(code), _TRUNCATE, L"%d", ret);
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_START_SERVICE_FAILED, service->exe, service->name, ret, 0);
		return ret;
	}
	log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_STARTED_SERVICE, service->exe, service->flags, service->name, service->dir, 0);

	/* Monitor service */
	if (!RegisterWaitForSingleObject(&service->wait_handle, service->process_handle, end_service, (void*)service, INFINITE, WT_EXECUTEONLYONCE | WT_EXECUTELONGFUNCTION))
	{
		log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_REGISTERWAITFORSINGLEOBJECT_FAILED, service->name, service->exe, error_string(GetLastError()), 0);
	}

	return 0;
}

wchar_t* service_control_text(uint32_t control)
{
	switch (control)
	{
	/* HACK: there is no SERVICE_CONTROL_START constant */
	case wait::controlstart:
		return L"START";
	case SERVICE_CONTROL_STOP:
		return L"STOP";
	case SERVICE_CONTROL_SHUTDOWN:
		return L"SHUTDOWN";
	case SERVICE_CONTROL_PAUSE:
		return L"PAUSE";
	case SERVICE_CONTROL_CONTINUE:
		return L"CONTINUE";
	case SERVICE_CONTROL_INTERROGATE:
		return L"INTERROGATE";
	case wait::controlrotate:
		return L"ROTATE";
	case SERVICE_CONTROL_POWEREVENT:
		return L"POWEREVENT";
	default:
		return 0;
	}
}

wchar_t* service_status_text(uint32_t status)
{
	switch (status)
	{
	case SERVICE_STOPPED:
		return L"SERVICE_STOPPED";
	case SERVICE_START_PENDING:
		return L"SERVICE_START_PENDING";
	case SERVICE_STOP_PENDING:
		return L"SERVICE_STOP_PENDING";
	case SERVICE_RUNNING:
		return L"SERVICE_RUNNING";
	case SERVICE_CONTINUE_PENDING:
		return L"SERVICE_CONTINUE_PENDING";
	case SERVICE_PAUSE_PENDING:
		return L"SERVICE_PAUSE_PENDING";
	case SERVICE_PAUSED:
		return L"SERVICE_PAUSED";
	default:
		return 0;
	}
}

void log_service_control(wchar_t* service_name, uint32_t control, bool handled)
{
	wchar_t* text = service_control_text(control);
	uint32_t event;

	if (!text)
	{
		/* "0x" + 8 x hex + nullptr */
		text = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, 11 * sizeof(wchar_t));
		if (!text)
		{
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"control code", L"log_service_control()", 0);
			return;
		}
		if (::_snwprintf_s(text, 11, _TRUNCATE, L"0x%08x", control) < 0)
		{
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"control code", L"log_service_control()", 0);
			HeapFree(GetProcessHeap(), 0, text);
			return;
		}

		event = NSSM_EVENT_SERVICE_CONTROL_UNKNOWN;
	}
	else if (handled)
		event = NSSM_EVENT_SERVICE_CONTROL_HANDLED;
	else
		event = NSSM_EVENT_SERVICE_CONTROL_NOT_HANDLED;

	log_event(EVENTLOG_INFORMATION_TYPE, event, service_name, text, 0);

	if (event == NSSM_EVENT_SERVICE_CONTROL_UNKNOWN)
	{
		HeapFree(GetProcessHeap(), 0, text);
	}
}

/* Service control handler */
uint32_t WINAPI service_control_handler(uint32_t control, uint32_t event, void* data, void* context)
{
	nssm_service_t* service = (nssm_service_t*)context;

	switch (control)
	{
	case SERVICE_CONTROL_INTERROGATE:
		/* We always keep the service status up-to-date so this is a no-op. */
		return NO_ERROR;

	case SERVICE_CONTROL_SHUTDOWN:
	case SERVICE_CONTROL_STOP:
		service->last_control = control;
		log_service_control(service->name, control, true);

		/* Immediately block further controls. */
		service->allow_restart = false;
		service->status.dwCurrentState = SERVICE_STOP_PENDING;
		service->status.dwControlsAccepted = 0;
		SetServiceStatus(service->status_handle, &service->status);

		/* Pre-stop hook. */
		nssm_hook(&hook_threads, service, hook::eventstop.data(), hook::actionpre.data(), &control, wait::statusdeadline, false);

		/*
        We MUST acknowledge the stop request promptly but we're committed to
        waiting for the application to exit.  Spawn a new thread to wait
        while we acknowledge the request.
      */
		if (!CreateThread(nullptr, 0, shutdown_service, context, 0, nullptr))
		{
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATETHREAD_FAILED, error_string(GetLastError()), 0);

			/*
          We couldn't create a thread to tidy up so we'll have to force the tidyup
          to complete in time in this thread.
        */
			service->kill_console_delay = wait::kill_console_grace_period;
			service->kill_window_delay = wait::kill_window_grace_period;
			service->kill_threads_delay = wait::kill_threads_grace_period;

			stop_service(service, 0, true, true);
		}
		return NO_ERROR;

	case SERVICE_CONTROL_CONTINUE:
		service->last_control = control;
		log_service_control(service->name, control, true);
		service->throttle = 0;
		if (use_critical_section)
			imports.WakeConditionVariable(&service->throttle_condition);
		else
		{
			if (!service->throttle_timer)
				return ERROR_CALL_NOT_IMPLEMENTED;
			ZeroMemory(&service->throttle_duetime, sizeof(service->throttle_duetime));
			SetWaitableTimer(service->throttle_timer, &service->throttle_duetime, 0, 0, 0, 0);
		}
		/* We can't continue if the application is running! */
		if (!service->process_handle)
			service->status.dwCurrentState = SERVICE_CONTINUE_PENDING;
		service->status.dwWaitHint = throttle_milliseconds(service->throttle) + wait::waithintmargin;
		log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_RESET_THROTTLE, service->name, 0);
		SetServiceStatus(service->status_handle, &service->status);
		return NO_ERROR;

	case SERVICE_CONTROL_PAUSE:
		/*
        We don't accept pause messages but it isn't possible to register
        only for continue messages so we have to handle this case.
      */
		log_service_control(service->name, control, false);
		return ERROR_CALL_NOT_IMPLEMENTED;

	case wait::controlrotate:
		service->last_control = control;
		log_service_control(service->name, control, true);
		(void)nssm_hook(&hook_threads, service, hook::eventrotate.data(), hook::actionpre.data(), &control, wait::hookdeadline, false);
		if (service->rotate_stdout_online == NSSM_ROTATE_ONLINE)
			service->rotate_stdout_online = NSSM_ROTATE_ONLINE_ASAP;
		if (service->rotate_stderr_online == NSSM_ROTATE_ONLINE)
			service->rotate_stderr_online = NSSM_ROTATE_ONLINE_ASAP;
		(void)nssm_hook(&hook_threads, service, hook::eventrotate.data(), hook::actionpost.data(), &control);
		return NO_ERROR;

	case SERVICE_CONTROL_POWEREVENT:
		/* Resume from suspend. */
		if (event == PBT_APMRESUMEAUTOMATIC)
		{
			service->last_control = control;
			log_service_control(service->name, control, true);
			(void)nssm_hook(&hook_threads, service, hook::eventpower.data(), hook::actionresume.data(), &control);
			return NO_ERROR;
		}

		/* Battery low or changed to A/C power or something. */
		if (event == PBT_APMPOWERSTATUSCHANGE)
		{
			service->last_control = control;
			log_service_control(service->name, control, true);
			(void)nssm_hook(&hook_threads, service, hook::eventpower.data(), hook::actionchange.data(), &control);
			return NO_ERROR;
		}
		log_service_control(service->name, control, false);
		return NO_ERROR;
	}

	/* Unknown control */
	log_service_control(service->name, control, false);
	return ERROR_CALL_NOT_IMPLEMENTED;
}

/* Start the service */
int32_t start_service(nssm_service_t* service)
{
	service->stopping = false;

	if (service->process_handle)
		return 0;
	service->start_requested_count++;

	/* Allocate a STARTUPINFOW structure for a new process */
	STARTUPINFOW si;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);

	/* Allocate a PROCESSINFO structure for the process */
	PROCESS_INFORMATION pi;
	ZeroMemory(&pi, sizeof(pi));

	/* Get startup parameters */
	int32_t ret = get_parameters(service, &si);
	if (ret)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GET_PARAMETERS_FAILED, service->name, 0);
		unset_service_environment(service);
		return stop_service(service, 2, true, true);
	}

	/* Launch executable with arguments */
	wchar_t cmd[CMD_LENGTH];
	if (::_snwprintf_s(cmd, std::size(cmd), _TRUNCATE, L"\"%s\" %s", service->exe, service->flags) < 0)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"command line", L"start_service", 0);
		unset_service_environment(service);
		return stop_service(service, 2, true, true);
	}

	throttle_restart(service);

	service->status.dwCurrentState = SERVICE_START_PENDING;
	service->status.dwControlsAccepted = SERVICE_ACCEPT_POWEREVENT | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_STOP;
	SetServiceStatus(service->status_handle, &service->status);

	uint32_t control = wait::controlstart;

	/* Did another thread receive a stop control? */
	if (service->allow_restart)
	{
		/* Set up I/O redirection. */
		if (get_output_handles(service, &si))
		{
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GET_OUTPUT_HANDLES_FAILED, service->name, 0);
			FreeConsole();
			close_output_handles(&si);
			unset_service_environment(service);
			return stop_service(service, 4, true, true);
		}
		FreeConsole();

		/* Pre-start hook. May need I/O to have been redirected already. */
		if (nssm_hook(&hook_threads, service, hook::eventstart.data(), hook::actionpre.data(), &control, wait::statusdeadline, false) == nssmhook::abort)
		{
			wchar_t code[16];
			::_snwprintf_s(code, std::size(code), _TRUNCATE, L"%u", nssmhook::abort);
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_PRESTART_HOOK_ABORT, hook::eventstart.data(), hook::actionpre.data(), service->name, code, 0);
			unset_service_environment(service);
			return stop_service(service, 5, true, true);
		}

		/* The pre-start hook will have cleaned the environment. */
		set_service_environment(service);

		bool inherit_handles = false;
		if (si.dwFlags & STARTF_USESTDHANDLES)
			inherit_handles = true;
		uint32_t flags = service->priority & priority_mask();
		if (service->affinity)
			flags |= CREATE_SUSPENDED;
		if (!service->no_console)
			flags |= CREATE_NEW_CONSOLE;
		if (!::CreateProcessW(0, cmd, 0, 0, inherit_handles, flags, 0, service->dir, &si, &pi))
		{
			uint32_t exitcode = 3;
			uint32_t error = GetLastError();
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEPROCESS_FAILED, service->name, service->exe, error_string(error), 0);
			close_output_handles(&si);
			unset_service_environment(service);
			return stop_service(service, exitcode, true, true);
		}
		service->start_count++;
		service->process_handle = pi.hProcess;
		service->pid = pi.dwProcessId;

		if (get_process_creation_time(service->process_handle, &service->creation_time))
			ZeroMemory(&service->creation_time, sizeof(service->creation_time));

		close_output_handles(&si);

		if (service->affinity)
		{
			/*
        We are explicitly storing service->affinity as a 64-bit uint32_t integer
        so that we can parse it regardless of whether we're running in 32-bit
        or 64-bit mode.  The arguments to SetProcessAffinityMask(), however, are
        defined as type uintptr_t and hence limited to 32 bits on a 32-bit system
        (or when running the 32-bit NSSM).

        The result is a lot of seemingly-unnecessary casting throughout the code
        and potentially confusion when we actually try to start the service.
        Having said that, however, it's unlikely that we're actually going to
        run in 32-bit mode on a system which has more than 32 CPUs so the
        likelihood of seeing a confusing situation is somewhat diminished.
      */
			uintptr_t affinity, system_affinity;

			if (GetProcessAffinityMask(service->process_handle, &affinity, &system_affinity))
				affinity = service->affinity & system_affinity;
			else
			{
				affinity = (uintptr_t)service->affinity;
				log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GETPROCESSAFFINITYMASK_FAILED, service->name, error_string(GetLastError()), 0);
			}

			if (!SetProcessAffinityMask(service->process_handle, affinity))
			{
				log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_SETPROCESSAFFINITYMASK_FAILED, service->name, error_string(GetLastError()), 0);
			}

			ResumeThread(pi.hThread);
		}
	}

	/* Restore our environment. */
	unset_service_environment(service);

	/*
    Wait for a clean startup before changing the service status to RUNNING
    but be mindful of the fact that we are blocking the service control manager
    so abandon the wait before too much time has elapsed.
  */
	if (await_single_handle(service->status_handle, &service->status, service->process_handle, service->name, L"start_service", service->throttle_delay) == 1)
		service->throttle = 0;

	/* Did another thread receive a stop control? */
	if (!service->allow_restart)
		return 0;

	/* Signal successful start */
	service->status.dwCurrentState = SERVICE_RUNNING;
	service->status.dwControlsAccepted &= ~SERVICE_ACCEPT_PAUSE_CONTINUE;
	SetServiceStatus(service->status_handle, &service->status);

	/* Post-start hook. */
	if (!service->throttle)
	{
		(void)nssm_hook(&hook_threads, service, hook::eventstart.data(), hook::actionpost.data(), &control);
	}

	/* Ensure the restart delay is always applied. */
	if (service->restart_delay && !service->throttle)
		service->throttle++;

	return 0;
}

/* Stop the service */
int32_t stop_service(nssm_service_t* service, uint32_t exitcode, bool graceful, bool default_action)
{
	service->allow_restart = false;
	if (service->wait_handle)
	{
		UnregisterWait(service->wait_handle);
		service->wait_handle = 0;
	}

	service->rotate_stdout_online = service->rotate_stderr_online = NSSM_ROTATE_OFFLINE;

	if (default_action && !exitcode && !graceful)
	{
		log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_GRACEFUL_SUICIDE, service->name, service->exe, exit_action_strings[exit::unclean], exit_action_strings[exit::unclean], exit_action_strings[exit::unclean], exit_action_strings[exit::really], 0);
		graceful = true;
	}

	/* Signal we are stopping */
	if (graceful)
	{
		service->status.dwCurrentState = SERVICE_STOP_PENDING;
		service->status.dwWaitHint = wait::waithintmargin;
		SetServiceStatus(service->status_handle, &service->status);
	}

	/* Nothing to do if service isn't running */
	if (service->pid)
	{
		/* Shut down service */
		log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_TERMINATEPROCESS, service->name, service->exe, 0);
		kill_t k;
		service_kill_t(service, &k);
		k.exitcode = 0;
		kill_process(&k);
	}
	else
		log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_PROCESS_ALREADY_STOPPED, service->name, service->exe, 0);

	end_service((void*)service, true);

	/* Signal we stopped */
	if (graceful)
	{
		service->status.dwCurrentState = SERVICE_STOP_PENDING;
		wait_for_hooks(service, true);
		service->status.dwCurrentState = SERVICE_STOPPED;
		if (exitcode)
		{
			service->status.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
			service->status.dwServiceSpecificExitCode = exitcode;
		}
		else
		{
			service->status.dwWin32ExitCode = NO_ERROR;
			service->status.dwServiceSpecificExitCode = 0;
		}
		SetServiceStatus(service->status_handle, &service->status);
	}

	return exitcode;
}

/* Callback function triggered when the server exits */
void CALLBACK end_service(void* arg, uint8_t why)
{
	nssm_service_t* service = (nssm_service_t*)arg;

	if (service->stopping)
		return;

	service->stopping = true;

	service->rotate_stdout_online = service->rotate_stderr_online = NSSM_ROTATE_OFFLINE;

	/* Use now as a dummy exit time. */
	GetSystemTimeAsFileTime(&service->exit_time);

	/* Check exit code */
	uint32_t exitcode = 0;
	wchar_t code[16];
	if (service->process_handle)
	{
		GetExitCodeProcess(service->process_handle, &exitcode);
		service->exitcode = exitcode;
		/* Check real exit time. */
		if (exitcode != STILL_ACTIVE)
			get_process_exit_time(service->process_handle, &service->exit_time);
		CloseHandle(service->process_handle);
	}

	service->process_handle = 0;

	/*
    Log that the service ended BEFORE logging about killing the process
    tree.  See below for the possible values of the why argument.
  */
	if (!why)
	{
		::_snwprintf_s(code, std::size(code), _TRUNCATE, L"%u", exitcode);
		log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_ENDED_SERVICE, service->exe, service->name, code, 0);
	}

	/* Clean up. */
	if (exitcode == STILL_ACTIVE)
		exitcode = 0;
	if (service->pid && service->kill_process_tree)
	{
		kill_t k;
		service_kill_t(service, &k);
		kill_process_tree(&k, service->pid);
	}
	service->pid = 0;

	/* Exit hook. */
	service->exit_count++;
	(void)nssm_hook(&hook_threads, service, hook::eventexit.data(), hook::actionpost.data(), nullptr, wait::hookdeadline, true);

	/* Exit logging threads. */
	cleanup_loggers(service);

	/*
    The why argument is true if our wait timed out or false otherwise.
    Our wait is infinite so why will never be true when called by the system.
    If it is indeed true, assume we were called from stop_service() because
    this is a controlled shutdown, and don't take any restart action.
  */
	if (why)
		return;
	if (!service->allow_restart)
		return;

	/* What action should we take? */
	int32_t action = exit::restart;
	wchar_t action_string[ACTION_LEN];
	bool default_action;
	if (!get_exit_action(service->name, &exitcode, action_string, &default_action))
	{
		for (int32_t i = 0; exit_action_strings[i]; i++)
		{
			if (!_wcsnicmp((const wchar_t*)action_string, exit_action_strings[i], ACTION_LEN))
			{
				action = i;
				break;
			}
		}
	}

	switch (action)
	{
	/* Try to restart the service or return failure code to service manager */
	case exit::restart:
		log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_EXIT_RESTART, service->name, code, exit_action_strings[action], service->exe, 0);
		while (monitor_service(service))
		{
			log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_RESTART_SERVICE_FAILED, service->exe, service->name, 0);
			Sleep(30000);
		}
		break;

	/* Do nothing, just like srvany would */
	case exit::ignore:
		log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_EXIT_IGNORE, service->name, code, exit_action_strings[action], service->exe, 0);
		wait_for_hooks(service, false);
		Sleep(INFINITE);
		break;

	/* Tell the service manager we are finished */
	case exit::really:
		log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_EXIT_REALLY, service->name, code, exit_action_strings[action], 0);
		stop_service(service, exitcode, true, default_action);
		break;

	/* Fake a crash so pre-Vista service managers will run recovery actions. */
	case exit::unclean:
		log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_EXIT_UNCLEAN, service->name, code, exit_action_strings[action], 0);
		stop_service(service, exitcode, false, default_action);
		wait_for_hooks(service, false);
		nssm_exit(exitcode);
	}
}

void throttle_restart(nssm_service_t* service)
{
	/* This can't be a restart if the service is already running. */
	if (!service->throttle++)
		return;

	uint32_t ms;
	uint32_t throttle_ms = throttle_milliseconds(service->throttle);
	wchar_t threshold[8], milliseconds[8];

	if (service->restart_delay > throttle_ms)
		ms = service->restart_delay;
	else
		ms = throttle_ms;

	::_snwprintf_s(milliseconds, std::size(milliseconds), _TRUNCATE, L"%u", ms);

	if (service->throttle == 1 && service->restart_delay > throttle_ms)
		log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_RESTART_DELAY, service->name, milliseconds, 0);
	else
	{
		::_snwprintf_s(threshold, std::size(threshold), _TRUNCATE, L"%u", service->throttle_delay);
		log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_THROTTLED, service->name, threshold, milliseconds, 0);
	}

	if (use_critical_section)
		EnterCriticalSection(&service->throttle_section);
	else if (service->throttle_timer)
	{
		ZeroMemory(&service->throttle_duetime, sizeof(service->throttle_duetime));
		service->throttle_duetime.QuadPart = 0 - (ms * 10000LL);
		SetWaitableTimer(service->throttle_timer, &service->throttle_duetime, 0, 0, 0, 0);
	}

	service->status.dwCurrentState = SERVICE_PAUSED;
	service->status.dwControlsAccepted |= SERVICE_ACCEPT_PAUSE_CONTINUE;
	SetServiceStatus(service->status_handle, &service->status);

	if (use_critical_section)
	{
		imports.SleepConditionVariableCS(&service->throttle_condition, &service->throttle_section, ms);
		LeaveCriticalSection(&service->throttle_section);
	}
	else
	{
		if (service->throttle_timer)
			WaitForSingleObject(service->throttle_timer, INFINITE);
		else
			Sleep(ms);
	}
}

/*
  When responding to a stop (or any other) request we need to set dwWaitHint to
  the number of milliseconds we expect the operation to take, and optionally
  increase dwCheckPoint.  If dwWaitHint milliseconds elapses without the
  operation completing or dwCheckPoint increasing, the system will consider the
  service to be hung.

  However the system will consider the service to be hung after 30000
  milliseconds regardless of the value of dwWaitHint if dwCheckPoint has not
  changed.  Therefore if we want to wait longer than that we must periodically
  increase dwCheckPoint.

  Furthermore, it will consider the service to be hung after 60000 milliseconds
  regardless of the value of dwCheckPoint unless dwWaitHint is increased every
  time dwCheckPoint is also increased.

  Our strategy then is to retrieve the initial dwWaitHint and wait for
  wait::statusdeadline milliseconds.  If the process is still running
  and we haven't finished waiting we increment dwCheckPoint and add whichever is
  smaller of wait::statusdeadline or the remaining timeout to
  dwWaitHint.

  Only doing both these things will prevent the system from killing the service.

  If the status_handle and service_status arguments are omitted, this function
  will not try to update the service manager but it will still log to the
  event log that it is waiting for a handle.

  Returns: 1 if the wait timed out.
           0 if the wait completed.
          -1 on error.
*/
int32_t await_single_handle(SERVICE_STATUS_HANDLE status_handle, SERVICE_STATUS* status, HANDLE handle, wchar_t* name, wchar_t* function_name, uint32_t timeout)
{
	uint32_t interval;
	uint32_t ret;
	uint32_t waited;
	wchar_t interval_milliseconds[16];
	wchar_t timeout_milliseconds[16];
	wchar_t waited_milliseconds[16];
	wchar_t* function = function_name;

	/* Add brackets to function name. */
	size_t funclen = ::wcslen(function_name) + 3;
	wchar_t* func = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, funclen * sizeof(wchar_t));
	if (func)
	{
		if (::_snwprintf_s(func, funclen, _TRUNCATE, L"%s()", function_name) > -1)
			function = func;
	}

	::_snwprintf_s(timeout_milliseconds, std::size(timeout_milliseconds), _TRUNCATE, L"%u", timeout);

	waited = 0;
	while (waited < timeout)
	{
		interval = timeout - waited;
		if (interval > wait::statusdeadline)
			interval = wait::statusdeadline;

		if (status)
		{
			status->dwWaitHint += interval;
			status->dwCheckPoint++;
			SetServiceStatus(status_handle, status);
		}

		if (waited)
		{
			::_snwprintf_s(waited_milliseconds, std::size(waited_milliseconds), _TRUNCATE, L"%u", waited);
			::_snwprintf_s(interval_milliseconds, std::size(interval_milliseconds), _TRUNCATE, L"%u", interval);
			log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_AWAITING_SINGLE_HANDLE, function, name, waited_milliseconds, interval_milliseconds, timeout_milliseconds, 0);
		}

		switch (WaitForSingleObject(handle, interval))
		{
		case WAIT_OBJECT_0:
			ret = 0;
			goto awaited;

		case WAIT_TIMEOUT:
			ret = 1;
			break;

		default:
			ret = -1;
			goto awaited;
		}

		waited += interval;
	}

awaited:
	if (func)
		HeapFree(GetProcessHeap(), 0, func);

	return ret;
}

int32_t list_nssm_services(int32_t argc, wchar_t** argv)
{
	bool including_native = (argc > 0 && str_equiv(argv[0], L"all"));

	/* Open service manager. */
	SC_HANDLE services = open_service_manager(SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
	if (!services)
	{
		print_message(stderr, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
		return 1;
	}

	uint32_t bufsize, required, count, i;
	uint32_t resume = 0;
	EnumServicesStatusEx(services, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, 0, 0, &required, &count, &resume, 0);
	uint32_t error = GetLastError();
	if (error != ERROR_MORE_DATA)
	{
		print_message(stderr, NSSM_MESSAGE_ENUMSERVICESSTATUS_FAILED, error_string(GetLastError()));
		return 2;
	}

	ENUM_SERVICE_STATUS_PROCESSW* status = (ENUM_SERVICE_STATUS_PROCESSW*)HeapAlloc(GetProcessHeap(), 0, required);
	if (!status)
	{
		print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"ENUM_SERVICE_STATUS_PROCESSW", L"list_nssm_services()");
		return 3;
	}

	bufsize = required;
	while (true)
	{
		int32_t ret = EnumServicesStatusEx(services, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, (uint8_t*)status, bufsize, &required, &count, &resume, 0);
		if (!ret)
		{
			error = GetLastError();
			if (error != ERROR_MORE_DATA)
			{
				HeapFree(GetProcessHeap(), 0, status);
				print_message(stderr, NSSM_MESSAGE_ENUMSERVICESSTATUS_FAILED, error_string(GetLastError()));
				return 4;
			}
		}

		for (i = 0; i < count; i++)
		{
			/* Try to get the service parameters. */
			nssm_service_t* service = alloc_nssm_service();
			if (!service)
			{
				HeapFree(GetProcessHeap(), 0, status);
				print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"nssm_service_t", L"list_nssm_services()");
				return 5;
			}
			::_snwprintf_s(service->name, std::size(service->name), _TRUNCATE, L"%s", status[i].lpServiceName);

			get_parameters(service, 0);
			/* We manage the service if we have an Application. */
			if (including_native || service->exe[0])
				wprintf(L"%s\n", service->name);

			cleanup_nssm_service(service);
		}

		if (ret)
			break;
	}

	HeapFree(GetProcessHeap(), 0, status);
	return 0;
}

int32_t service_process_tree(int32_t argc, wchar_t** argv)
{
	int32_t errors = 0;
	if (argc < 1)
		return usage(1);

	SC_HANDLE services = open_service_manager(SC_MANAGER_CONNECT);
	if (!services)
	{
		print_message(stderr, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
		return 1;
	}

	/*
    We need SeDebugPrivilege to read the process tree.
    We ignore failure here so that an error will be printed later when we
    try to open a process handle.
  */
	HANDLE token = get_debug_token();

	wchar_t canonical_name[SERVICE_NAME_LENGTH];
	SERVICE_STATUS_PROCESS service_status;
	nssm_service_t* service;
	kill_t k;

	int32_t i;
	for (i = 0; i < argc; i++)
	{
		wchar_t* service_name = argv[i];
		SC_HANDLE service_handle = open_service(services, service_name, SERVICE_QUERY_STATUS, canonical_name, std::size(canonical_name));
		if (!service_handle)
		{
			errors++;
			continue;
		}

		uint32_t size;
		int32_t ret = QueryServiceStatusEx(service_handle, SC_STATUS_PROCESS_INFO, (uint8_t*)&service_status, sizeof(service_status), &size);
		int32_t error = GetLastError();
		CloseServiceHandle(service_handle);
		if (!ret)
		{
			fwprintf(stderr, L"%s: %s\n", canonical_name, error_string(error));
			errors++;
			continue;
		}

		ZeroMemory(&k, sizeof(k));
		k.pid = service_status.dwProcessId;
		if (!k.pid)
			continue;

		k.process_handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, k.pid);
		if (!k.process_handle)
		{
			fwprintf(stderr, L"%s: %u: %s\n", canonical_name, k.pid, error_string(GetLastError()));
			continue;
		}

		if (get_process_creation_time(k.process_handle, &k.creation_time))
			continue;
		/* Dummy exit time so we can check processes' parents. */
		GetSystemTimeAsFileTime(&k.exit_time);

		service = alloc_nssm_service();
		if (!service)
		{
			errors++;
			continue;
		}

		::_snwprintf_s(service->name, std::size(service->name), _TRUNCATE, L"%s", canonical_name);
		k.name = service->name;
		walk_process_tree(service, print_process, &k, k.pid);

		cleanup_nssm_service(service);
	}

	CloseServiceHandle(services);
	if (token != INVALID_HANDLE_VALUE)
		CloseHandle(token);

	return errors;
}
