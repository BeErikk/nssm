/*******************************************************************************
 hook.cpp - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#include "nssm_pch.h"
#include "common.h"

#include "hook.h"

struct hook_t
{
	wchar_t* name;
	HANDLE process_handle;
	uint32_t pid;
	uint32_t deadline;
	FILETIME creation_time;
	kill_t k;
};

const wchar_t* hook_event_strings[] = {hook::eventstart.data(), hook::eventstop.data(), hook::eventexit.data(), hook::eventpower.data(), hook::eventrotate.data(), nullptr};
const wchar_t* hook_action_strings[] = {hook::actionpre.data(), hook::actionpost.data(), hook::actionchange.data(), hook::actionresume.data(), nullptr};

// LPTHREAD_START_ROUTINE
static ULONG __stdcall await_hook(void* arg)
{
	hook_t* hook = (hook_t*)arg;
	if (!hook)
		return std::to_underlying(nssmhook::error);

	nssmhook ret = nssmhook::none;
	if (WaitForSingleObject(hook->process_handle, hook->deadline) == WAIT_TIMEOUT)
		ret = nssmhook::timeout;

	/* Tidy up hook process tree. */
	if (hook->name)
		hook->k.name = hook->name;
	else
		hook->k.name = L"hook";
	hook->k.process_handle = hook->process_handle;
	hook->k.pid = hook->pid;
	hook->k.stop_method = ~0u;
	hook->k.kill_console_delay = wait::kill_console_grace_period;
	hook->k.kill_window_delay = wait::kill_window_grace_period;
	hook->k.kill_threads_delay = wait::kill_threads_grace_period;
	hook->k.creation_time = hook->creation_time;
	GetSystemTimeAsFileTime(&hook->k.exit_time);
	kill_process_tree(&hook->k, hook->pid);

	if (ret != nssmhook::none)
	{
		CloseHandle(hook->process_handle);
		if (hook->name)
			HeapFree(GetProcessHeap(), 0, hook->name);
		HeapFree(GetProcessHeap(), 0, hook);
		return std::to_underlying(ret);
	}

	ULONG exitcode;
	::GetExitCodeProcess(hook->process_handle, &exitcode);
	CloseHandle(hook->process_handle);

	if (hook->name)
		HeapFree(GetProcessHeap(), 0, hook->name);
	HeapFree(GetProcessHeap(), 0, hook);

	if (exitcode == std::to_underlying(nssmhook::abort))
		return exitcode;
	if (exitcode)
		return std::to_underlying(nssmhook::failed);

	return std::to_underlying(nssmhook::success);
}

static void set_hook_runtime(wchar_t* v, FILETIME* start, FILETIME* now)
{
	if (start && now)
	{
		ULARGE_INTEGER s;
		s.LowPart = start->dwLowDateTime;
		s.HighPart = start->dwHighDateTime;
		if (s.QuadPart)
		{
			ULARGE_INTEGER t;
			t.LowPart = now->dwLowDateTime;
			t.HighPart = now->dwHighDateTime;
			if (t.QuadPart && t.QuadPart >= s.QuadPart)
			{
				t.QuadPart -= s.QuadPart;
				t.QuadPart /= 10000LL;
				wchar_t number[16];
				::_snwprintf_s(number, std::size(number), _TRUNCATE, L"%llu", t.QuadPart);
				::SetEnvironmentVariableW(v, number);
				return;
			}
		}
	}
	::SetEnvironmentVariableW(v, L"");
}

static void add_thread_handle(hook_thread_t* hook_threads, HANDLE thread_handle, wchar_t* name)
{
	if (!hook_threads)
		return;

	int32_t num_threads = hook_threads->num_threads + 1;
	hook_thread_data_t* data = (hook_thread_data_t*)HeapAlloc(GetProcessHeap(), 0, num_threads * sizeof(hook_thread_data_t));
	if (!data)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"hook_thread_t", L"add_thread_handle()", 0);
		return;
	}

	int32_t i;
	for (i = 0; i < hook_threads->num_threads; i++)
		memmove(&data[i], &hook_threads->data[i], sizeof(data[i]));
	memmove(data[i].name, name, sizeof(data[i].name));
	data[i].thread_handle = thread_handle;

	if (hook_threads->data)
		HeapFree(GetProcessHeap(), 0, hook_threads->data);
	hook_threads->data = data;
	hook_threads->num_threads = num_threads;
}

bool valid_hook_name(const wchar_t* hook_event, const wchar_t* hook_action, bool quiet)
{
	bool valid_event = false;
	bool valid_action = false;

	/* Exit/Post */
	if (str_equiv(hook_event, hook::eventexit.data()))
	{
		if (str_equiv(hook_action, hook::actionpost.data()))
			return true;
		if (quiet)
			return false;
		print_message(stderr, NSSM_MESSAGE_INVALID_HOOK_ACTION, hook_event);
		fwprintf(stderr, L"%s\n", hook::actionpost.data());
		return false;
	}

	/* Power/{Change,Resume} */
	if (str_equiv(hook_event, hook::eventpower.data()))
	{
		if (str_equiv(hook_action, hook::actionchange.data()))
			return true;
		if (str_equiv(hook_action, hook::actionresume.data()))
			return true;
		if (quiet)
			return false;
		print_message(stderr, NSSM_MESSAGE_INVALID_HOOK_ACTION, hook_event);
		fwprintf(stderr, L"%s\n", hook::actionchange.data());
		fwprintf(stderr, L"%s\n", hook::actionresume.data());
		return false;
	}

	/* Rotate/{Pre,Post} */
	if (str_equiv(hook_event, hook::eventrotate.data()))
	{
		if (str_equiv(hook_action, hook::actionpre.data()))
			return true;
		if (str_equiv(hook_action, hook::actionpost.data()))
			return true;
		if (quiet)
			return false;
		print_message(stderr, NSSM_MESSAGE_INVALID_HOOK_ACTION, hook_event);
		fwprintf(stderr, L"%s\n", hook::actionpre.data());
		fwprintf(stderr, L"%s\n", hook::actionpost.data());
		return false;
	}

	/* Start/{Pre,Post} */
	if (str_equiv(hook_event, hook::eventstart.data()))
	{
		if (str_equiv(hook_action, hook::actionpre.data()))
			return true;
		if (str_equiv(hook_action, hook::actionpost.data()))
			return true;
		if (quiet)
			return false;
		print_message(stderr, NSSM_MESSAGE_INVALID_HOOK_ACTION, hook_event);
		fwprintf(stderr, L"%s\n", hook::actionpre.data());
		fwprintf(stderr, L"%s\n", hook::actionpost.data());
		return false;
	}

	/* Stop/Pre */
	if (str_equiv(hook_event, hook::eventstop.data()))
	{
		if (str_equiv(hook_action, hook::actionpre.data()))
			return true;
		if (quiet)
			return false;
		print_message(stderr, NSSM_MESSAGE_INVALID_HOOK_ACTION, hook_event);
		fwprintf(stderr, L"%s\n", hook::actionpre.data());
		return false;
	}

	if (quiet)
		return false;
	print_message(stderr, NSSM_MESSAGE_INVALID_HOOK_EVENT);
	fwprintf(stderr, L"%s\n", hook::eventexit.data());
	fwprintf(stderr, L"%s\n", hook::eventpower.data());
	fwprintf(stderr, L"%s\n", hook::eventrotate.data());
	fwprintf(stderr, L"%s\n", hook::eventstart.data());
	fwprintf(stderr, L"%s\n", hook::eventstop.data());
	return false;
}

void await_hook_threads(hook_thread_t* hook_threads, SERVICE_STATUS_HANDLE status_handle, SERVICE_STATUS* status, uint32_t deadline)
{
	if (!hook_threads)
		return;
	if (!hook_threads->num_threads)
		return;

	int32_t* retain = (int32_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, hook_threads->num_threads * sizeof(int32_t));
	if (!retain)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"retain", L"await_hook_threads()", 0);
		return;
	}

	/*
    We could use WaitForMultipleObjects() but await_single_object() can update
    the service status as well.
  */
	int32_t num_threads = 0;
	int32_t i;
	for (i = 0; i < hook_threads->num_threads; i++)
	{
		if (deadline)
		{
			if (await_single_handle(status_handle, status, hook_threads->data[i].thread_handle, hook_threads->data[i].name, _T(__FUNCTION__), deadline) != 1)
			{
				CloseHandle(hook_threads->data[i].thread_handle);
				continue;
			}
		}
		else if (WaitForSingleObject(hook_threads->data[i].thread_handle, 0) != WAIT_TIMEOUT)
		{
			CloseHandle(hook_threads->data[i].thread_handle);
			continue;
		}

		retain[num_threads++] = i;
	}

	if (num_threads)
	{
		hook_thread_data_t* data = (hook_thread_data_t*)HeapAlloc(GetProcessHeap(), 0, num_threads * sizeof(hook_thread_data_t));
		if (!data)
		{
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"data", L"await_hook_threads()", 0);
			HeapFree(GetProcessHeap(), 0, retain);
			return;
		}

		for (i = 0; i < num_threads; i++)
			memmove(&data[i], &hook_threads->data[retain[i]], sizeof(data[i]));

		HeapFree(GetProcessHeap(), 0, hook_threads->data);
		hook_threads->data = data;
		hook_threads->num_threads = num_threads;
	}
	else
	{
		HeapFree(GetProcessHeap(), 0, hook_threads->data);
		ZeroMemory(hook_threads, sizeof(*hook_threads));
	}

	HeapFree(GetProcessHeap(), 0, retain);
}

/*
   Returns:
   NSSM_HOOK_STATUS_SUCCESS  if the hook ran successfully.
   NSSM_HOOK_STATUS_NOTFOUND if no hook was found.
   NSSM_HOOK_STATUS_ABORT    if the hook failed and we should cancel service start.
   NSSM_HOOK_STATUS_ERROR    on error.
   NSSM_HOOK_STATUS_NOTRUN   if the hook didn't run.
   NSSM_HOOK_STATUS_TIMEOUT  if the hook timed out.
   NSSM_HOOK_STATUS_FAILED   if the hook failed.
*/
nssmhook nssm_hook(hook_thread_t* hook_threads, nssm_service_t* service, wchar_t* hook_event, wchar_t* hook_action, uint32_t* hook_control, uint32_t deadline, bool async)
{
	nssmhook ret = nssmhook::none;

	hook_t* hook = (hook_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(hook_t));
	if (!hook)
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, L"hook", L"nssm_hook()", 0);
		return nssmhook::error;
	}

	FILETIME now;
	GetSystemTimeAsFileTime(&now);

	EnterCriticalSection(&service->hook_section);

	/* Set the environment. */
	set_service_environment(service);

	/* ABI version. */
	wchar_t number[16];
	::_snwprintf_s(number, std::size(number), _TRUNCATE, L"%u", std::to_underlying(hookconst::version));
	::SetEnvironmentVariableW(hookenv::hookversion.data(), number);

	/* Event triggering this action. */
	::SetEnvironmentVariableW(hookenv::event.data(), hook_event);

	/* Hook action. */
	::SetEnvironmentVariableW(hookenv::action.data(), hook_action);

	/* Control triggering this action.  May be empty. */
	if (hook_control)
		::SetEnvironmentVariableW(hookenv::trigger.data(), service_control_text(*hook_control));
	else
		::SetEnvironmentVariableW(hookenv::trigger.data(), L"");

	/* Last control handled. */
	::SetEnvironmentVariableW(hookenv::lastcontrol.data(), service_control_text(service->last_control));

	/* Path to NSSM, unquoted for the environment. */
	::SetEnvironmentVariableW(hookenv::imagepath.data(), nssm_unquoted_imagepath());

	/* NSSM version. */
	::SetEnvironmentVariableW(hookenv::config.data(), NSSM_CONFIGURATION);
	::SetEnvironmentVariableW(hookenv::version.data(), NSSM_VERSION);
	::SetEnvironmentVariableW(hookenv::builddate.data(), NSSM_DATE);

	/* NSSM PID. */
	::_snwprintf_s(number, std::size(number), _TRUNCATE, L"%u", GetCurrentProcessId());
	::SetEnvironmentVariableW(hookenv::pid.data(), number);

	/* NSSM runtime. */
	set_hook_runtime(hookenv::runtime.data(), &service->nssm_creation_time, &now);

	/* Application PID. */
	if (service->pid)
	{
		::_snwprintf_s(number, std::size(number), _TRUNCATE, L"%u", service->pid);
		::SetEnvironmentVariableW(hookenv::apppid.data(), number);
		/* Application runtime. */
		set_hook_runtime(hookenv::appruntime.data(), &service->creation_time, &now);
		/* Exit code. */
		::SetEnvironmentVariableW(hookenv::exitcode.data(), L"");
	}
	else
	{
		::SetEnvironmentVariableW(hookenv::apppid.data(), L"");
		if (str_equiv(hook_event, hook::eventstart.data()) && str_equiv(hook_action, hook::actionpre.data()))
		{
			::SetEnvironmentVariableW(hookenv::appruntime.data(), L"");
			::SetEnvironmentVariableW(hookenv::exitcode.data(), L"");
		}
		else
		{
			set_hook_runtime(hookenv::appruntime.data(), &service->creation_time, &service->exit_time);
			/* Exit code. */
			::_snwprintf_s(number, std::size(number), _TRUNCATE, L"%u", service->exitcode);
			::SetEnvironmentVariableW(hookenv::exitcode.data(), number);
		}
	}

	/* Deadline for this script. */
	::_snwprintf_s(number, std::size(number), _TRUNCATE, L"%u", deadline);
	::SetEnvironmentVariableW(hookenv::deadline.data(), number);

	/* Service name. */
	::SetEnvironmentVariableW(hookenv::servicename.data(), service->name);
	::SetEnvironmentVariableW(hookenv::displayname.data(), service->displayname);

	/* Times the service was asked to start. */
	::_snwprintf_s(number, std::size(number), _TRUNCATE, L"%u", service->start_requested_count);
	::SetEnvironmentVariableW(hookenv::startrequestedcount.data(), number);

	/* Times the service actually did start. */
	::_snwprintf_s(number, std::size(number), _TRUNCATE, L"%u", service->start_count);
	::SetEnvironmentVariableW(hookenv::startcount.data(), number);

	/* Times the service exited. */
	::_snwprintf_s(number, std::size(number), _TRUNCATE, L"%u", service->exit_count);
	::SetEnvironmentVariableW(hookenv::exitcount.data(), number);

	/* Throttled count. */
	::_snwprintf_s(number, std::size(number), _TRUNCATE, L"%u", service->throttle);
	::SetEnvironmentVariableW(hookenv::throttlecount.data(), number);

	/* Command line. */
	wchar_t app[CMD_LENGTH];
	::_snwprintf_s(app, std::size(app), _TRUNCATE, L"\"%s\" %s", service->exe, service->flags);
	::SetEnvironmentVariableW(hookenv::commandline.data(), app);

	wchar_t cmd[CMD_LENGTH];
	if (get_hook(service->name, hook_event, hook_action, cmd, sizeof(cmd)))
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GET_HOOK_FAILED, hook_event, hook_action, service->name, 0);
		unset_service_environment(service);
		LeaveCriticalSection(&service->hook_section);
		HeapFree(GetProcessHeap(), 0, hook);
		return nssmhook::error;
	}

	/* No hook. */
	if (!::wcslen(cmd))
	{
		unset_service_environment(service);
		LeaveCriticalSection(&service->hook_section);
		HeapFree(GetProcessHeap(), 0, hook);
		return nssmhook::notfound;
	}

	/* Run the command. */
	STARTUPINFOW si;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi;
	ZeroMemory(&pi, sizeof(pi));
	if (service->hook_share_output_handles)
		(void)use_output_handles(service, &si);
	bool inherit_handles = false;
	if (si.dwFlags & STARTF_USESTDHANDLES)
		inherit_handles = true;
	uint32_t flags = 0;
#ifdef UNICODE
	flags |= CREATE_UNICODE_ENVIRONMENT;
#endif
	ret = nssmhook::notrun;
	if (::CreateProcessW(0, cmd, 0, 0, inherit_handles, flags, 0, service->dir, &si, &pi))
	{
		close_output_handles(&si);
		hook->name = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, hookconst::namelength * sizeof(wchar_t));
		if (hook->name)
			::_snwprintf_s(hook->name, hookconst::namelength, _TRUNCATE, L"%s (%s/%s)", service->name, hook_event, hook_action);
		hook->process_handle = pi.hProcess;
		hook->pid = pi.dwProcessId;
		hook->deadline = deadline;
		if (get_process_creation_time(hook->process_handle, &hook->creation_time))
			GetSystemTimeAsFileTime(&hook->creation_time);

		uint32_t tid;
		HANDLE thread_handle = CreateThread(nullptr, 0, await_hook, (void*)hook, 0, &tid);
		if (thread_handle)
		{
			if (async)
			{
				ret = 0;
				await_hook_threads(hook_threads, service->status_handle, &service->status, 0);
				add_thread_handle(hook_threads, thread_handle, hook->name);
			}
			else
			{
				await_single_handle(service->status_handle, &service->status, thread_handle, hook->name, _T(__FUNCTION__), deadline + wait::statusdeadline);
				uint32_t exitcode;
				GetExitCodeThread(thread_handle, &exitcode);
				ret = (int32_t)exitcode;
				CloseHandle(thread_handle);
			}
		}
		else
		{
			log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATETHREAD_FAILED, error_string(GetLastError()), 0);
			await_hook(hook);
			if (hook->name)
				HeapFree(GetProcessHeap(), 0, hook->name);
			HeapFree(GetProcessHeap(), 0, hook);
		}
	}
	else
	{
		log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_HOOK_CREATEPROCESS_FAILED, hook_event, hook_action, service->name, cmd, error_string(GetLastError()), 0);
		HeapFree(GetProcessHeap(), 0, hook);
		close_output_handles(&si);
	}

	/* Restore our environment. */
	unset_service_environment(service);

	LeaveCriticalSection(&service->hook_section);

	return ret;
}

nssmhook nssm_hook(hook_thread_t* hook_threads, nssm_service_t* service, wchar_t* hook_event, wchar_t* hook_action, uint32_t* hook_control, uint32_t deadline)
{
	return nssm_hook(hook_threads, service, hook_event, hook_action, hook_control, deadline, true);
}

nssmhook nssm_hook(hook_thread_t* hook_threads, nssm_service_t* service, wchar_t* hook_event, wchar_t* hook_action, uint32_t* hook_control)
{
	return nssm_hook(hook_threads, service, hook_event, hook_action, hook_control, wait::hookdeadline);
}
