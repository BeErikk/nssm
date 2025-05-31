/*******************************************************************************
 gui.cpp - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#include "nssm_pch.h"
#include "common.h"

#include "gui.h"

extern const wchar_t* hook_event_strings[];
extern const wchar_t* hook_action_strings[];

static enum { NSSM_TAB_APPLICATION,
	NSSM_TAB_DETAILS,
	NSSM_TAB_LOGON,
	NSSM_TAB_DEPENDENCIES,
	NSSM_TAB_PROCESS,
	NSSM_TAB_SHUTDOWN,
	NSSM_TAB_EXIT,
	NSSM_TAB_IO,
	NSSM_TAB_ROTATION,
	NSSM_TAB_ENVIRONMENT,
	NSSM_TAB_HOOKS,
	NSSM_NUM_TABS } nssm_tabs;
static HWND tablist[NSSM_NUM_TABS];
static int32_t selected_tab;

static HWND dialog(const wchar_t* templ, HWND parent, DLGPROC function, LPARAM l)
{
	/* The caller will deal with GetLastError()... */
	HRSRC resource = FindResourceEx(0, RT_DIALOG, templ, GetUserDefaultLangID());
	if (!resource)
	{
		if (GetLastError() != ERROR_RESOURCE_LANG_NOT_FOUND)
			return 0;
		resource = FindResourceEx(0, RT_DIALOG, templ, winapi::MakeLangid(LANG_NEUTRAL, SUBLANG_NEUTRAL));
		if (!resource)
			return 0;
	}

	HGLOBAL ret = LoadResource(0, resource);
	if (!ret)
		return 0;

	return ::CreateDialogIndirectParamW(0, (DLGTEMPLATE*)ret, parent, function, l);
}

static HWND dialog(const wchar_t* templ, HWND parent, DLGPROC function)
{
	return dialog(templ, parent, function, 0);
}

static inline void set_logon_enabled(uint8_t interact_enabled, uint8_t credentials_enabled)
{
	::EnableWindow(::GetDlgItem(tablist[NSSM_TAB_LOGON], IDC_INTERACT), interact_enabled);
	::EnableWindow(::GetDlgItem(tablist[NSSM_TAB_LOGON], IDC_USERNAME), credentials_enabled);
	::EnableWindow(::GetDlgItem(tablist[NSSM_TAB_LOGON], IDC_PASSWORD1), credentials_enabled);
	::EnableWindow(::GetDlgItem(tablist[NSSM_TAB_LOGON], IDC_PASSWORD2), credentials_enabled);
}

int32_t nssm_gui(int32_t resource, nssm_service_t* service)
{
	/* Create window */
	HWND dlg = dialog(util::MakeEnumResourcel(resource), 0, nssm_dlg, (LPARAM)service);
	if (!dlg)
	{
		popup_message(0, winapi::messboxflag::ok, NSSM_GUI_CREATEDIALOG_FAILED, error_string(GetLastError()));
		return 1;
	}

	/* Load the icon. */
	HANDLE icon = ::LoadImageW(::GetModuleHandleW(0), util::MakeEnumResourcel(IDI_NSSM), winapi::imagetype::icon, ::GetSystemMetrics(winapi::systemmetrics::cxsmicon), ::GetSystemMetrics(winapi::systemmetrics::cysmicon), 0);
	if (icon)
		::SendMessageW(dlg, WM_SETICON, winapi::wmgetseticontype::smallicon, (LPARAM)icon);
	icon = ::LoadImageW(::GetModuleHandleW(0), util::MakeEnumResourcel(IDI_NSSM), winapi::imagetype::icon, ::GetSystemMetrics(winapi::systemmetrics::cxicon), ::GetSystemMetrics(winapi::systemmetrics::cyicon), 0);
	if (icon)
		::SendMessageW(dlg, WM_SETICON, winapi::wmgetseticontype::bigicon, (LPARAM)icon);

	/* Remember what the window is for. */
	::SetWindowLongPtrW(dlg, winapi::winlongindex::userdata, (intptr_t)resource);

	/* Display the window */
	centre_window(dlg);
	::ShowWindow(dlg, winapi::wndshowstate::show);

	/* Set service name if given */
	if (service->name[0])
	{
		::SetDlgItemTextW(dlg, IDC_NAME, service->name);
		/* No point making user click remove if the name is already entered */
		if (resource == IDD_REMOVE)
		{
			HWND button = ::GetDlgItem(dlg, IDC_REMOVE);
			if (button)
			{
				::SendMessageW(button, WM_LBUTTONDOWN, 0, 0);
				::SendMessageW(button, WM_LBUTTONUP, 0, 0);
			}
		}
	}

	if (resource == IDD_EDIT)
	{
		/* We'll need the service handle later. */
		::SetWindowLongPtrW(dlg, winapi::winlongindex::user, (intptr_t)service);

		/* Service name can't be edited. */
		::EnableWindow(::GetDlgItem(dlg, IDC_NAME), 0);
		::SetFocus(::GetDlgItem(dlg, IDOK));

		/* Set existing details. */
		HWND combo;
		HWND list;

		/* Application tab. */
		if (service->native)
			::SetDlgItemTextW(tablist[NSSM_TAB_APPLICATION], IDC_PATH, service->image);
		else
			::SetDlgItemTextW(tablist[NSSM_TAB_APPLICATION], IDC_PATH, service->exe);
		::SetDlgItemTextW(tablist[NSSM_TAB_APPLICATION], IDC_DIR, service->dir);
		::SetDlgItemTextW(tablist[NSSM_TAB_APPLICATION], IDC_FLAGS, service->flags);

		/* Details tab. */
		::SetDlgItemTextW(tablist[NSSM_TAB_DETAILS], IDC_DISPLAYNAME, service->displayname);
		::SetDlgItemTextW(tablist[NSSM_TAB_DETAILS], IDC_DESCRIPTION, service->description);
		combo = ::GetDlgItem(tablist[NSSM_TAB_DETAILS], IDC_STARTUP);
		::SendMessageW(combo, CB_SETCURSEL, service->startup, 0);

		/* Log on tab. */
		if (service->username)
		{
			if (is_virtual_account(service->name, service->username))
			{
				::CheckRadioButton(tablist[NSSM_TAB_LOGON], IDC_LOCALSYSTEM, IDC_VIRTUAL_SERVICE, IDC_VIRTUAL_SERVICE);
				set_logon_enabled(0, 0);
			}
			else
			{
				::CheckRadioButton(tablist[NSSM_TAB_LOGON], IDC_LOCALSYSTEM, IDC_VIRTUAL_SERVICE, IDC_ACCOUNT);
				::SetDlgItemTextW(tablist[NSSM_TAB_LOGON], IDC_USERNAME, service->username);
				set_logon_enabled(0, 1);
			}
		}
		else
		{
			::CheckRadioButton(tablist[NSSM_TAB_LOGON], IDC_LOCALSYSTEM, IDC_VIRTUAL_SERVICE, IDC_LOCALSYSTEM);
			if (service->type & SERVICE_INTERACTIVE_PROCESS)
				::SendDlgItemMessageW(tablist[NSSM_TAB_LOGON], IDC_INTERACT, BM_SETCHECK, winapi::buttonstate::checked, 0);
		}

		/* Dependencies tab. */
		if (service->dependencieslen)
		{
			wchar_t* formatted;
			uint32_t newlen;
			if (format_double_null(service->dependencies, service->dependencieslen, &formatted, &newlen))
			{
				popup_message(dlg, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_EVENT_OUT_OF_MEMORY, L"dependencies", L"nssm_dlg()");
			}
			else
			{
				::SetDlgItemTextW(tablist[NSSM_TAB_DEPENDENCIES], IDC_DEPENDENCIES, formatted);
				HeapFree(GetProcessHeap(), 0, formatted);
			}
		}

		/* Process tab. */
		if (service->priority)
		{
			int32_t priority = priority_constant_to_index(service->priority);
			combo = ::GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_PRIORITY);
			::SendMessageW(combo, CB_SETCURSEL, priority, 0);
		}

		if (service->affinity)
		{
			list = ::GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY);
			::SendDlgItemMessageW(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY_ALL, BM_SETCHECK, winapi::buttonstate::unchecked, 0);
			::EnableWindow(::GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY), 1);

			uintptr_t affinity, system_affinity;
			if (GetProcessAffinityMask(GetCurrentProcess(), &affinity, &system_affinity))
			{
				if ((service->affinity & (int64_t)system_affinity) != service->affinity)
					popup_message(dlg, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_WARN_AFFINITY);
			}

			for (int32_t i = 0; i < num_cpus(); i++)
			{
				if (!(service->affinity & (1LL << (int64_t)i)))
					::SendMessageW(list, LB_SETSEL, 0, i);
			}
		}

		if (service->no_console)
		{
			::SendDlgItemMessageW(tablist[NSSM_TAB_PROCESS], IDC_CONSOLE, BM_SETCHECK, winapi::buttonstate::unchecked, 0);
		}

		/* Shutdown tab. */
		if (!(service->stop_method & stopmethod::console))
		{
			::SendDlgItemMessageW(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_CONSOLE, BM_SETCHECK, winapi::buttonstate::unchecked, 0);
			::EnableWindow(::GetDlgItem(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_CONSOLE), 0);
		}
		::SetDlgItemInt(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_CONSOLE, service->kill_console_delay, 0);
		if (!(service->stop_method & stopmethod::window))
		{
			::SendDlgItemMessageW(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_WINDOW, BM_SETCHECK, winapi::buttonstate::unchecked, 0);
			::EnableWindow(::GetDlgItem(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_WINDOW), 0);
		}
		::SetDlgItemInt(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_WINDOW, service->kill_window_delay, 0);
		if (!(service->stop_method & stopmethod::threads))
		{
			::SendDlgItemMessageW(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_THREADS, BM_SETCHECK, winapi::buttonstate::unchecked, 0);
			::EnableWindow(::GetDlgItem(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_THREADS), 0);
		}
		::SetDlgItemInt(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_THREADS, service->kill_threads_delay, 0);
		if (!(service->stop_method & stopmethod::terminate))
		{
			::SendDlgItemMessageW(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_TERMINATE, BM_SETCHECK, winapi::buttonstate::unchecked, 0);
		}
		if (!service->kill_process_tree)
		{
			::SendDlgItemMessageW(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_PROCESS_TREE, BM_SETCHECK, winapi::buttonstate::unchecked, 0);
		}

		/* Restart tab. */
		::SetDlgItemInt(tablist[NSSM_TAB_EXIT], IDC_THROTTLE, service->throttle_delay, 0);
		combo = ::GetDlgItem(tablist[NSSM_TAB_EXIT], IDC_APPEXIT);
		::SendMessageW(combo, CB_SETCURSEL, service->default_exit_action, 0);
		::SetDlgItemInt(tablist[NSSM_TAB_EXIT], IDC_RESTART_DELAY, service->restart_delay, 0);

		/* I/O tab. */
		::SetDlgItemTextW(tablist[NSSM_TAB_IO], IDC_STDIN, service->stdin_path);
		::SetDlgItemTextW(tablist[NSSM_TAB_IO], IDC_STDOUT, service->stdout_path);
		::SetDlgItemTextW(tablist[NSSM_TAB_IO], IDC_STDERR, service->stderr_path);
		if (service->timestamp_log)
			::SendDlgItemMessageW(tablist[NSSM_TAB_IO], IDC_TIMESTAMP, BM_SETCHECK, winapi::buttonstate::checked, 0);

		/* Rotation tab. */
		if (service->stdout_disposition == CREATE_ALWAYS)
			::SendDlgItemMessageW(tablist[NSSM_TAB_ROTATION], IDC_TRUNCATE, BM_SETCHECK, winapi::buttonstate::checked, 0);
		if (service->rotate_files)
		{
			::SendDlgItemMessageW(tablist[NSSM_TAB_ROTATION], IDC_ROTATE, BM_SETCHECK, winapi::buttonstate::checked, 0);
			::EnableWindow(::GetDlgItem(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_ONLINE), 1);
			::EnableWindow(::GetDlgItem(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_SECONDS), 1);
			::EnableWindow(::GetDlgItem(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_BYTES_LOW), 1);
		}
		if (service->rotate_stdout_online || service->rotate_stderr_online)
			::SendDlgItemMessageW(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_ONLINE, BM_SETCHECK, winapi::buttonstate::checked, 0);
		::SetDlgItemInt(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_SECONDS, service->rotate_seconds, 0);
		if (!service->rotate_bytes_high)
			::SetDlgItemInt(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_BYTES_LOW, service->rotate_bytes_low, 0);

		/* Hooks tab. */
		if (service->hook_share_output_handles)
			::SendDlgItemMessageW(tablist[NSSM_TAB_HOOKS], IDC_REDIRECT_HOOK, BM_SETCHECK, winapi::buttonstate::checked, 0);

		/* Check if advanced settings are in use. */
		if (service->stdout_disposition != service->stderr_disposition || (service->stdout_disposition && service->stdout_disposition != NSSM_STDOUT_DISPOSITION && service->stdout_disposition != CREATE_ALWAYS) || (service->stderr_disposition && service->stderr_disposition != NSSM_STDERR_DISPOSITION && service->stderr_disposition != CREATE_ALWAYS))
			popup_message(dlg, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_WARN_STDIO);
		if (service->rotate_bytes_high)
			popup_message(dlg, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_WARN_ROTATE_BYTES);

		/* Environment tab. */
		wchar_t* env;
		uint32_t envlen;
		if (service->env_extralen)
		{
			env = service->env_extra;
			envlen = service->env_extralen;
		}
		else
		{
			env = service->env;
			envlen = service->envlen;
			if (envlen)
				::SendDlgItemMessageW(tablist[NSSM_TAB_ENVIRONMENT], IDC_ENVIRONMENT_REPLACE, BM_SETCHECK, winapi::buttonstate::checked, 0);
		}

		if (envlen)
		{
			wchar_t* formatted;
			uint32_t newlen;
			if (format_double_null(env, envlen, &formatted, &newlen))
			{
				popup_message(dlg, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_EVENT_OUT_OF_MEMORY, L"environment", L"nssm_dlg()");
			}
			else
			{
				::SetDlgItemTextW(tablist[NSSM_TAB_ENVIRONMENT], IDC_ENVIRONMENT, formatted);
				HeapFree(GetProcessHeap(), 0, formatted);
			}
		}
		if (service->envlen && service->env_extralen)
			popup_message(dlg, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_WARN_ENVIRONMENT);
	}

	/* Go! */
	MSG message;
	while (::GetMessageW(&message, 0, 0, 0))
	{
		if (::IsDialogMessageW(dlg, &message))
			continue;
		::TranslateMessage(&message);
		::DispatchMessageW(&message);
	}

	return (int32_t)message.wParam;
}

void centre_window(HWND window)
{
	HWND desktop;
	RECT size, desktop_size;
	uint32_t x, y;

	if (!window)
		return;

	/* Find window size */
	if (!::GetWindowRect(window, &size))
		return;

	/* Find desktop window */
	desktop = ::GetDesktopWindow();
	if (!desktop)
		return;

	/* Find desktop window size */
	if (!::GetWindowRect(desktop, &desktop_size))
		return;

	/* Centre window */
	x = (desktop_size.right - size.right) / 2;
	y = (desktop_size.bottom - size.bottom) / 2;
	::MoveWindow(window, x, y, size.right - size.left, size.bottom - size.top, 0);
}

static inline void check_stop_method(nssm_service_t* service, uint32_t method, uint32_t control)
{
	if (::SendDlgItemMessageW(tablist[NSSM_TAB_SHUTDOWN], control, BM_GETCHECK, 0, 0) & winapi::buttonstate::checked)
		return;
	service->stop_method &= ~method;
}

static inline void check_number(HWND tab, uint32_t control, uint32_t* timeout)
{
	BOOL translated;
	uint32_t configured = ::GetDlgItemInt(tab, control, &translated, 0);
	if (translated)
		*timeout = configured;
}

static inline void set_timeout_enabled(uint32_t control, uint32_t dependent)
{
	uint8_t enabled = 0;
	if (::SendDlgItemMessageW(tablist[NSSM_TAB_SHUTDOWN], control, BM_GETCHECK, 0, 0) & winapi::buttonstate::checked)
		enabled = 1;
	::EnableWindow(::GetDlgItem(tablist[NSSM_TAB_SHUTDOWN], dependent), enabled);
}

static inline void set_affinity_enabled(uint8_t enabled)
{
	::EnableWindow(::GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY), enabled);
}

static inline void set_rotation_enabled(uint8_t enabled)
{
	::EnableWindow(::GetDlgItem(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_ONLINE), enabled);
	::EnableWindow(::GetDlgItem(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_SECONDS), enabled);
	::EnableWindow(::GetDlgItem(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_BYTES_LOW), enabled);
}

static inline int32_t hook_env(const wchar_t* hook_event, const wchar_t* hook_action, wchar_t* buffer, uint32_t buflen)
{
	return ::_snwprintf_s(buffer, buflen, _TRUNCATE, L"NSSM_HOOK_%s_%s", hook_event, hook_action);
}

static inline void set_hook_tab(int32_t event_index, int32_t action_index, bool changed)
{
	int32_t first_event = NSSM_GUI_HOOK_EVENT_START;
	HWND combo;
	combo = ::GetDlgItem(tablist[NSSM_TAB_HOOKS], IDC_HOOK_EVENT);
	::SendMessageW(combo, CB_SETCURSEL, event_index, 0);
	combo = ::GetDlgItem(tablist[NSSM_TAB_HOOKS], IDC_HOOK_ACTION);
	::SendMessageW(combo, CB_RESETCONTENT, 0, 0);

	const wchar_t* hook_event = hook_event_strings[event_index];
	wchar_t* hook_action;
	int32_t i;
	switch (event_index + first_event)
	{
	case NSSM_GUI_HOOK_EVENT_ROTATE:
		i = 0;
		::SendMessageW(combo, CB_INSERTSTRING, i, (LPARAM)message_string(NSSM_GUI_HOOK_ACTION_ROTATE_PRE));
		if (action_index == i++)
			hook_action = hook::actionpre.data();
		::SendMessageW(combo, CB_INSERTSTRING, i, (LPARAM)message_string(NSSM_GUI_HOOK_ACTION_ROTATE_POST));
		if (action_index == i++)
			hook_action = hook::actionpost.data();
		break;

	case NSSM_GUI_HOOK_EVENT_START:
		i = 0;
		::SendMessageW(combo, CB_INSERTSTRING, i, (LPARAM)message_string(NSSM_GUI_HOOK_ACTION_START_PRE));
		if (action_index == i++)
			hook_action = hook::actionpre.data();
		::SendMessageW(combo, CB_INSERTSTRING, i, (LPARAM)message_string(NSSM_GUI_HOOK_ACTION_START_POST));
		if (action_index == i++)
			hook_action = hook::actionpost.data();
		break;

	case NSSM_GUI_HOOK_EVENT_STOP:
		i = 0;
		::SendMessageW(combo, CB_INSERTSTRING, i, (LPARAM)message_string(NSSM_GUI_HOOK_ACTION_STOP_PRE));
		if (action_index == i++)
			hook_action = hook::actionpre.data();
		break;

	case NSSM_GUI_HOOK_EVENT_EXIT:
		i = 0;
		::SendMessageW(combo, CB_INSERTSTRING, i, (LPARAM)message_string(NSSM_GUI_HOOK_ACTION_EXIT_POST));
		if (action_index == i++)
			hook_action = hook::actionpost.data();
		break;

	case NSSM_GUI_HOOK_EVENT_POWER:
		i = 0;
		::SendMessageW(combo, CB_INSERTSTRING, i, (LPARAM)message_string(NSSM_GUI_HOOK_ACTION_POWER_CHANGE));
		if (action_index == i++)
			hook_action = hook::actionchange.data();
		::SendMessageW(combo, CB_INSERTSTRING, i, (LPARAM)message_string(NSSM_GUI_HOOK_ACTION_POWER_RESUME));
		if (action_index == i++)
			hook_action = hook::actionresume.data();
		break;
	}

	::SendMessageW(combo, CB_SETCURSEL, action_index, 0);

	wchar_t hook_name[hookconst::namelength];
	hook_env(hook_event, hook_action, hook_name, std::size(hook_name));

	if (!*hook_name)
		return;

	wchar_t cmd[CMD_LENGTH];
	if (changed)
	{
		::GetDlgItemTextW(tablist[NSSM_TAB_HOOKS], IDC_HOOK, cmd, std::size(cmd));
		::SetEnvironmentVariableW(hook_name, cmd);
	}
	else
	{
		if (!::GetEnvironmentVariableW(hook_name, cmd, std::size(cmd)))
			cmd[0] = _T('\0');
		::SetDlgItemTextW(tablist[NSSM_TAB_HOOKS], IDC_HOOK, cmd);
	}
}

static inline int32_t update_hook(wchar_t* service_name, const wchar_t* hook_event, const wchar_t* hook_action)
{
	wchar_t hook_name[hookconst::namelength];
	if (hook_env(hook_event, hook_action, hook_name, std::size(hook_name)) < 0)
		return 1;
	wchar_t cmd[CMD_LENGTH];
	ZeroMemory(cmd, sizeof(cmd));
	::GetEnvironmentVariableW(hook_name, cmd, std::size(cmd));
	if (set_hook(service_name, hook_event, hook_action, cmd))
		return 2;
	return 0;
}

static inline int32_t update_hooks(wchar_t* service_name)
{
	int32_t ret = 0;
	ret += update_hook(service_name, hook::eventstart.data(), hook::actionpre.data());
	ret += update_hook(service_name, hook::eventstart.data(), hook::actionpost.data());
	ret += update_hook(service_name, hook::eventstop.data(), hook::actionpre.data());
	ret += update_hook(service_name, hook::eventexit.data(), hook::actionpost.data());
	ret += update_hook(service_name, hook::eventpower.data(), hook::actionchange.data());
	ret += update_hook(service_name, hook::eventpower.data(), hook::actionresume.data());
	ret += update_hook(service_name, hook::eventrotate.data(), hook::actionpre.data());
	ret += update_hook(service_name, hook::eventrotate.data(), hook::actionpost.data());
	return ret;
}

static inline void check_io(HWND owner, wchar_t* name, wchar_t* buffer, uint32_t len, uint32_t control)
{
	if (!::SendMessageW(::GetDlgItem(tablist[NSSM_TAB_IO], control), WM_GETTEXTLENGTH, 0, 0))
		return;
	if (::GetDlgItemTextW(tablist[NSSM_TAB_IO], control, buffer, (int32_t)len))
		return;
	popup_message(owner, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_MESSAGE_PATH_TOO_LONG, name);
	ZeroMemory(buffer, len * sizeof(wchar_t));
}

/* Set service parameters. */
int32_t configure(HWND window, nssm_service_t* service, nssm_service_t* orig_service)
{
	if (!service)
		return 1;

	set_nssm_service_defaults(service);

	if (orig_service)
	{
		service->native = orig_service->native;
		service->handle = orig_service->handle;
	}

	/* Get service name. */
	if (!::GetDlgItemTextW(window, IDC_NAME, service->name, std::size(service->name)))
	{
		popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_MISSING_SERVICE_NAME);
		cleanup_nssm_service(service);
		return 2;
	}

	/* Get executable name */
	if (!service->native)
	{
		if (!::GetDlgItemTextW(tablist[NSSM_TAB_APPLICATION], IDC_PATH, service->exe, std::size(service->exe)))
		{
			popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_MISSING_PATH);
			return 3;
		}

		/* Get startup directory. */
		if (!::GetDlgItemTextW(tablist[NSSM_TAB_APPLICATION], IDC_DIR, service->dir, std::size(service->dir)))
		{
			::_snwprintf_s(service->dir, std::size(service->dir), _TRUNCATE, L"%s", service->exe);
			strip_basename(service->dir);
		}

		/* Get flags. */
		if (::SendMessageW(::GetDlgItem(tablist[NSSM_TAB_APPLICATION], IDC_FLAGS), WM_GETTEXTLENGTH, 0, 0))
		{
			if (!::GetDlgItemTextW(tablist[NSSM_TAB_APPLICATION], IDC_FLAGS, service->flags, std::size(service->flags)))
			{
				popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_INVALID_OPTIONS);
				return 4;
			}
		}
	}

	/* Get details. */
	if (::SendMessageW(::GetDlgItem(tablist[NSSM_TAB_DETAILS], IDC_DISPLAYNAME), WM_GETTEXTLENGTH, 0, 0))
	{
		if (!::GetDlgItemTextW(tablist[NSSM_TAB_DETAILS], IDC_DISPLAYNAME, service->displayname, std::size(service->displayname)))
		{
			popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_INVALID_DISPLAYNAME);
			return 5;
		}
	}

	if (::SendMessageW(::GetDlgItem(tablist[NSSM_TAB_DETAILS], IDC_DESCRIPTION), WM_GETTEXTLENGTH, 0, 0))
	{
		if (!::GetDlgItemTextW(tablist[NSSM_TAB_DETAILS], IDC_DESCRIPTION, service->description, std::size(service->description)))
		{
			popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_INVALID_DESCRIPTION);
			return 5;
		}
	}

	HWND combo = ::GetDlgItem(tablist[NSSM_TAB_DETAILS], IDC_STARTUP);
	service->startup = (uint32_t)::SendMessageW(combo, CB_GETCURSEL, 0, 0);
	if (service->startup == winapi::combobresult::err)
		service->startup = 0;

	/* Get logon stuff. */
	if (::SendDlgItemMessageW(tablist[NSSM_TAB_LOGON], IDC_LOCALSYSTEM, BM_GETCHECK, 0, 0) & winapi::buttonstate::checked)
	{
		if (::SendDlgItemMessageW(tablist[NSSM_TAB_LOGON], IDC_INTERACT, BM_GETCHECK, 0, 0) & winapi::buttonstate::checked)
		{
			service->type |= SERVICE_INTERACTIVE_PROCESS;
		}
		if (service->username)
			HeapFree(GetProcessHeap(), 0, service->username);
		service->username = 0;
		service->usernamelen = 0;
		if (service->password)
		{
			SecureZeroMemory(service->password, service->passwordlen * sizeof(wchar_t));
			HeapFree(GetProcessHeap(), 0, service->password);
		}
		service->password = 0;
		service->passwordlen = 0;
	}
	else if (::SendDlgItemMessageW(tablist[NSSM_TAB_LOGON], IDC_VIRTUAL_SERVICE, BM_GETCHECK, 0, 0) & winapi::buttonstate::checked)
	{
		if (service->username)
			HeapFree(GetProcessHeap(), 0, service->username);
		service->username = virtual_account(service->name);
		if (!service->username)
		{
			popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_EVENT_OUT_OF_MEMORY, L"account name", L"install()");
			return 6;
		}
		service->usernamelen = ::wcslen(service->username) + 1;
		service->password = 0;
		service->passwordlen = 0;
	}
	else
	{
		/* Username. */
		service->usernamelen = ::SendMessageW(::GetDlgItem(tablist[NSSM_TAB_LOGON], IDC_USERNAME), WM_GETTEXTLENGTH, 0, 0);
		if (!service->usernamelen)
		{
			popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_MISSING_USERNAME);
			return 6;
		}
		service->usernamelen++;

		service->username = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, service->usernamelen * sizeof(wchar_t));
		if (!service->username)
		{
			popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_EVENT_OUT_OF_MEMORY, L"account name", L"install()");
			return 6;
		}
		if (!::GetDlgItemTextW(tablist[NSSM_TAB_LOGON], IDC_USERNAME, service->username, (int32_t)service->usernamelen))
		{
			HeapFree(GetProcessHeap(), 0, service->username);
			service->username = 0;
			service->usernamelen = 0;
			popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_INVALID_USERNAME);
			return 6;
		}

		/*
      Special case for well-known accounts.
      Ignore the password if we're editing and the username hasn't changed.
    */
		const wchar_t* well_known = well_known_username(service->username);
		if (well_known)
		{
			if (str_equiv(well_known, NSSM_LOCALSYSTEM_ACCOUNT))
			{
				HeapFree(GetProcessHeap(), 0, service->username);
				service->username = 0;
				service->usernamelen = 0;
			}
			else
			{
				service->usernamelen = ::wcslen(well_known) + 1;
				service->username = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, service->usernamelen * sizeof(wchar_t));
				if (!service->username)
				{
					print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"canon", L"install()");
					return 6;
				}
				memmove(service->username, well_known, service->usernamelen * sizeof(wchar_t));
			}
		}
		else
		{
			/* Password. */
			service->passwordlen = ::SendMessageW(::GetDlgItem(tablist[NSSM_TAB_LOGON], IDC_PASSWORD1), WM_GETTEXTLENGTH, 0, 0);
			size_t passwordlen = ::SendMessageW(::GetDlgItem(tablist[NSSM_TAB_LOGON], IDC_PASSWORD2), WM_GETTEXTLENGTH, 0, 0);

			if (!orig_service || !orig_service->username || !str_equiv(service->username, orig_service->username) || service->passwordlen || passwordlen)
			{
				if (!service->passwordlen)
				{
					popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_MISSING_PASSWORD);
					return 6;
				}
				if (passwordlen != service->passwordlen)
				{
					popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_MISSING_PASSWORD);
					return 6;
				}
				service->passwordlen++;

				/* Temporary buffer for password validation. */
				wchar_t* password = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, service->passwordlen * sizeof(wchar_t));
				if (!password)
				{
					HeapFree(GetProcessHeap(), 0, service->username);
					service->username = 0;
					service->usernamelen = 0;
					popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_EVENT_OUT_OF_MEMORY, L"password confirmation", L"install()");
					return 6;
				}

				/* Actual password buffer. */
				service->password = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, service->passwordlen * sizeof(wchar_t));
				if (!service->password)
				{
					HeapFree(GetProcessHeap(), 0, password);
					HeapFree(GetProcessHeap(), 0, service->username);
					service->username = 0;
					service->usernamelen = 0;
					popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_EVENT_OUT_OF_MEMORY, L"password", L"install()");
					return 6;
				}

				/* Get first password. */
				if (!::GetDlgItemTextW(tablist[NSSM_TAB_LOGON], IDC_PASSWORD1, service->password, (int32_t)service->passwordlen))
				{
					HeapFree(GetProcessHeap(), 0, password);
					SecureZeroMemory(service->password, service->passwordlen * sizeof(wchar_t));
					HeapFree(GetProcessHeap(), 0, service->password);
					service->password = 0;
					service->passwordlen = 0;
					HeapFree(GetProcessHeap(), 0, service->username);
					service->username = 0;
					service->usernamelen = 0;
					popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_INVALID_PASSWORD);
					return 6;
				}

				/* Get confirmation. */
				if (!::GetDlgItemTextW(tablist[NSSM_TAB_LOGON], IDC_PASSWORD2, password, (int32_t)service->passwordlen))
				{
					SecureZeroMemory(password, service->passwordlen * sizeof(wchar_t));
					HeapFree(GetProcessHeap(), 0, password);
					SecureZeroMemory(service->password, service->passwordlen * sizeof(wchar_t));
					HeapFree(GetProcessHeap(), 0, service->password);
					service->password = 0;
					service->passwordlen = 0;
					HeapFree(GetProcessHeap(), 0, service->username);
					service->username = 0;
					service->usernamelen = 0;
					popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_INVALID_PASSWORD);
					return 6;
				}

				/* Compare. */
				if (wcsncmp(password, service->password, service->passwordlen))
				{
					popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_MISSING_PASSWORD);
					SecureZeroMemory(password, service->passwordlen * sizeof(wchar_t));
					HeapFree(GetProcessHeap(), 0, password);
					SecureZeroMemory(service->password, service->passwordlen * sizeof(wchar_t));
					HeapFree(GetProcessHeap(), 0, service->password);
					service->password = 0;
					service->passwordlen = 0;
					HeapFree(GetProcessHeap(), 0, service->username);
					service->username = 0;
					service->usernamelen = 0;
					return 6;
				}
			}
		}
	}

	/* Get dependencies. */
	uint32_t dependencieslen = (uint32_t)::SendMessageW(::GetDlgItem(tablist[NSSM_TAB_DEPENDENCIES], IDC_DEPENDENCIES), WM_GETTEXTLENGTH, 0, 0);
	if (dependencieslen)
	{
		wchar_t* dependencies = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (dependencieslen + 2) * sizeof(wchar_t));
		if (!dependencies)
		{
			popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_EVENT_OUT_OF_MEMORY, L"dependencies", L"install()");
			cleanup_nssm_service(service);
			return 6;
		}

		if (!::GetDlgItemTextW(tablist[NSSM_TAB_DEPENDENCIES], IDC_DEPENDENCIES, dependencies, dependencieslen + 1))
		{
			popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_INVALID_DEPENDENCIES);
			HeapFree(GetProcessHeap(), 0, dependencies);
			cleanup_nssm_service(service);
			return 6;
		}

		if (unformat_double_null(dependencies, dependencieslen, &service->dependencies, &service->dependencieslen))
		{
			HeapFree(GetProcessHeap(), 0, dependencies);
			popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_EVENT_OUT_OF_MEMORY, L"dependencies", L"install()");
			cleanup_nssm_service(service);
			return 6;
		}

		HeapFree(GetProcessHeap(), 0, dependencies);
	}

	/* Remaining tabs are only for services we manage. */
	if (service->native)
		return 0;

	/* Get process stuff. */
	combo = ::GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_PRIORITY);
	service->priority = priority_index_to_constant((uint32_t)::SendMessageW(combo, CB_GETCURSEL, 0, 0));

	service->affinity = 0LL;
	if (!(::SendDlgItemMessageW(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY_ALL, BM_GETCHECK, 0, 0) & winapi::buttonstate::checked))
	{
		HWND list = ::GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY);
		int32_t selected = (int32_t)::SendMessageW(list, LB_GETSELCOUNT, 0, 0);
		int32_t count = (int32_t)::SendMessageW(list, LB_GETCOUNT, 0, 0);
		if (!selected)
		{
			popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_WARN_AFFINITY_NONE);
			return 5;
		}
		else if (selected < count)
		{
			for (int32_t i = 0; i < count; i++)
			{
				if (::SendMessageW(list, LB_GETSEL, i, 0))
					service->affinity |= (1LL << (int64_t)i);
			}
		}
	}

	if (::SendDlgItemMessageW(tablist[NSSM_TAB_PROCESS], IDC_CONSOLE, BM_GETCHECK, 0, 0) & winapi::buttonstate::checked)
		service->no_console = 0;
	else
		service->no_console = 1;

	/* Get stop method stuff. */
	check_stop_method(service, stopmethod::console, IDC_METHOD_CONSOLE);
	check_stop_method(service, stopmethod::window, IDC_METHOD_WINDOW);
	check_stop_method(service, stopmethod::threads, IDC_METHOD_THREADS);
	check_stop_method(service, stopmethod::terminate, IDC_METHOD_TERMINATE);
	check_number(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_CONSOLE, &service->kill_console_delay);
	check_number(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_WINDOW, &service->kill_window_delay);
	check_number(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_THREADS, &service->kill_threads_delay);
	if (::SendDlgItemMessageW(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_PROCESS_TREE, BM_GETCHECK, 0, 0) & winapi::buttonstate::checked)
		service->kill_process_tree = 1;
	else
		service->kill_process_tree = 0;

	/* Get exit action stuff. */
	check_number(tablist[NSSM_TAB_EXIT], IDC_THROTTLE, &service->throttle_delay);
	combo = ::GetDlgItem(tablist[NSSM_TAB_EXIT], IDC_APPEXIT);
	service->default_exit_action = (uint32_t)::SendMessageW(combo, CB_GETCURSEL, 0, 0);
	if (service->default_exit_action == winapi::combobresult::err)
		service->default_exit_action = 0;
	check_number(tablist[NSSM_TAB_EXIT], IDC_RESTART_DELAY, &service->restart_delay);

	/* Get I/O stuff. */
	check_io(window, L"stdin", service->stdin_path, std::size(service->stdin_path), IDC_STDIN);
	check_io(window, L"stdout", service->stdout_path, std::size(service->stdout_path), IDC_STDOUT);
	check_io(window, L"stderr", service->stderr_path, std::size(service->stderr_path), IDC_STDERR);
	if (::SendDlgItemMessageW(tablist[NSSM_TAB_IO], IDC_TIMESTAMP, BM_GETCHECK, 0, 0) & winapi::buttonstate::checked)
		service->timestamp_log = true;
	else
		service->timestamp_log = false;

	/* Override stdout and/or stderr. */
	if (::SendDlgItemMessageW(tablist[NSSM_TAB_ROTATION], IDC_TRUNCATE, BM_GETCHECK, 0, 0) & winapi::buttonstate::checked)
	{
		if (service->stdout_path[0])
			service->stdout_disposition = CREATE_ALWAYS;
		if (service->stderr_path[0])
			service->stderr_disposition = CREATE_ALWAYS;
	}

	/* Get rotation stuff. */
	if (::SendDlgItemMessageW(tablist[NSSM_TAB_ROTATION], IDC_ROTATE, BM_GETCHECK, 0, 0) & winapi::buttonstate::checked)
	{
		service->rotate_files = true;
		if (::SendDlgItemMessageW(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_ONLINE, BM_GETCHECK, 0, 0) & winapi::buttonstate::checked)
			service->rotate_stdout_online = service->rotate_stderr_online = NSSM_ROTATE_ONLINE;
		check_number(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_SECONDS, &service->rotate_seconds);
		check_number(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_BYTES_LOW, &service->rotate_bytes_low);
	}

	/* Get hook stuff. */
	if (::SendDlgItemMessageW(tablist[NSSM_TAB_HOOKS], IDC_REDIRECT_HOOK, BM_GETCHECK, 0, 0) & winapi::buttonstate::checked)
		service->hook_share_output_handles = true;

	/* Get environment. */
	uint32_t envlen = (uint32_t)::SendMessageW(::GetDlgItem(tablist[NSSM_TAB_ENVIRONMENT], IDC_ENVIRONMENT), WM_GETTEXTLENGTH, 0, 0);
	if (envlen)
	{
		wchar_t* env = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (envlen + 2) * sizeof(wchar_t));
		if (!env)
		{
			popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_EVENT_OUT_OF_MEMORY, L"environment", L"install()");
			cleanup_nssm_service(service);
			return 5;
		}

		if (!::GetDlgItemTextW(tablist[NSSM_TAB_ENVIRONMENT], IDC_ENVIRONMENT, env, envlen + 1))
		{
			popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_INVALID_ENVIRONMENT);
			HeapFree(GetProcessHeap(), 0, env);
			cleanup_nssm_service(service);
			return 5;
		}

		wchar_t* newenv;
		uint32_t newlen;
		if (unformat_double_null(env, envlen, &newenv, &newlen))
		{
			HeapFree(GetProcessHeap(), 0, env);
			popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_EVENT_OUT_OF_MEMORY, L"environment", L"install()");
			cleanup_nssm_service(service);
			return 5;
		}

		HeapFree(GetProcessHeap(), 0, env);
		env = newenv;
		envlen = newlen;

		/* Test the environment is valid. */
		if (test_environment(env))
		{
			popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_INVALID_ENVIRONMENT);
			HeapFree(GetProcessHeap(), 0, env);
			cleanup_nssm_service(service);
			return 5;
		}

		if (::SendDlgItemMessageW(tablist[NSSM_TAB_ENVIRONMENT], IDC_ENVIRONMENT_REPLACE, BM_GETCHECK, 0, 0) & winapi::buttonstate::checked)
		{
			service->env = env;
			service->envlen = envlen;
		}
		else
		{
			service->env_extra = env;
			service->env_extralen = envlen;
		}
	}

	return 0;
}

/* Install the service. */
int32_t install(HWND window)
{
	if (!window)
		return 1;

	nssm_service_t* service = alloc_nssm_service();
	if (service)
	{
		int32_t ret = configure(window, service, 0);
		if (ret)
			return ret;
	}

	/* See if it works. */
	switch (install_service(service))
	{
	case 1:
		popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_EVENT_OUT_OF_MEMORY, L"service", L"install()");
		cleanup_nssm_service(service);
		return 1;

	case 2:
		popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
		cleanup_nssm_service(service);
		return 2;

	case 3:
		popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_MESSAGE_PATH_TOO_LONG, NSSM);
		cleanup_nssm_service(service);
		return 3;

	case 4:
		popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_OUT_OF_MEMORY_FOR_IMAGEPATH);
		cleanup_nssm_service(service);
		return 4;

	case 5:
		popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_INSTALL_SERVICE_FAILED);
		cleanup_nssm_service(service);
		return 5;

	case 6:
		popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_CREATE_PARAMETERS_FAILED);
		cleanup_nssm_service(service);
		return 6;
	}

	update_hooks(service->name);

	popup_message(window, winapi::messboxflag::ok, NSSM_MESSAGE_SERVICE_INSTALLED, service->name);
	cleanup_nssm_service(service);
	return 0;
}

/* Remove the service */
int32_t remove(HWND window)
{
	if (!window)
		return 1;

	/* See if it works */
	nssm_service_t* service = alloc_nssm_service();
	if (service)
	{
		/* Get service name */
		if (!::GetDlgItemTextW(window, IDC_NAME, service->name, std::size(service->name)))
		{
			popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_MISSING_SERVICE_NAME);
			cleanup_nssm_service(service);
			return 2;
		}

		/* Confirm */
		if (popup_message(window, winapi::messboxflag::yesno, NSSM_GUI_ASK_REMOVE_SERVICE, service->name) != IDYES)
		{
			cleanup_nssm_service(service);
			return 0;
		}
	}

	switch (remove_service(service))
	{
	case 1:
		popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_EVENT_OUT_OF_MEMORY, L"service", L"remove()");
		cleanup_nssm_service(service);
		return 1;

	case 2:
		popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
		cleanup_nssm_service(service);
		return 2;

	case 3:
		popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_SERVICE_NOT_INSTALLED);
		cleanup_nssm_service(service);
		return 3;

	case 4:
		popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_REMOVE_SERVICE_FAILED);
		cleanup_nssm_service(service);
		return 4;
	}

	popup_message(window, winapi::messboxflag::ok, NSSM_MESSAGE_SERVICE_REMOVED, service->name);
	cleanup_nssm_service(service);
	return 0;
}

int32_t edit(HWND window, nssm_service_t* orig_service)
{
	if (!window)
		return 1;

	nssm_service_t* service = alloc_nssm_service();
	if (service)
	{
		int32_t ret = configure(window, service, orig_service);
		if (ret)
			return ret;
	}

	switch (edit_service(service, true))
	{
	case 1:
		popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_EVENT_OUT_OF_MEMORY, L"service", L"edit()");
		cleanup_nssm_service(service);
		return 1;

	case 3:
		popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_MESSAGE_PATH_TOO_LONG, NSSM);
		cleanup_nssm_service(service);
		return 3;

	case 4:
		popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_OUT_OF_MEMORY_FOR_IMAGEPATH);
		cleanup_nssm_service(service);
		return 4;

	case 5:
	case 6:
		popup_message(window, winapi::messboxflag::ok | winapi::messboxflag::iconwarning, NSSM_GUI_EDIT_PARAMETERS_FAILED);
		cleanup_nssm_service(service);
		return 6;
	}

	update_hooks(service->name);

	popup_message(window, winapi::messboxflag::ok, NSSM_MESSAGE_SERVICE_EDITED, service->name);
	cleanup_nssm_service(service);
	return 0;
}

static wchar_t* browse_filter(int32_t message)
{
	switch (message)
	{
	case NSSM_GUI_BROWSE_FILTER_APPLICATIONS:
		return L"*.exe;*.bat;*.cmd";
	case NSSM_GUI_BROWSE_FILTER_DIRECTORIES:
		return L".";
	case NSSM_GUI_BROWSE_FILTER_ALL_FILES: /* Fall through. */
	default:
		return L"*.*";
	}
}

uintptr_t CALLBACK browse_hook(HWND dlg, uint32_t message, WPARAM w, LPARAM l)
{
	switch (message)
	{
	case WM_INITDIALOG:
		return 1;
	}

	return 0;
}

/* Browse for application */
void browse(HWND window, wchar_t* current, uint32_t flags, ...)
{
	if (!window)
		return;

	va_list arg;
	size_t bufsize = 256;
	size_t len = bufsize;
	int32_t i;

	OPENFILENAMEW ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrFilter = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, bufsize * sizeof(wchar_t));
	/* XXX: Escaping nulls with ::FormatMessageW is tricky */
	if (ofn.lpstrFilter)
	{
		ZeroMemory((void*)ofn.lpstrFilter, bufsize);
		len = 0;
		/* "Applications" + nullptr + "*.exe" + nullptr */
		va_start(arg, flags);
		while (i = va_arg(arg, int32_t))
		{
			wchar_t* localised = message_string(i);
			::_snwprintf_s((wchar_t*)ofn.lpstrFilter + len, bufsize - len, _TRUNCATE, localised);
			len += ::wcslen(localised) + 1;
			LocalFree(localised);
			wchar_t* filter = browse_filter(i);
			::_snwprintf_s((wchar_t*)ofn.lpstrFilter + len, bufsize - len, _TRUNCATE, L"%s", filter);
			len += ::wcslen(filter) + 1;
		}
		va_end(arg);
		/* Remainder of the buffer is already zeroed */
	}
	ofn.lpstrFile = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, nssmconst::pathlength * sizeof(wchar_t));
	if (ofn.lpstrFile)
	{
		if (flags & OFN_NOVALIDATE)
		{
			/* Directory hack. */
			::_snwprintf_s(ofn.lpstrFile, nssmconst::pathlength, _TRUNCATE, L":%s:", message_string(NSSM_GUI_BROWSE_FILTER_DIRECTORIES));
			ofn.nMaxFile = nssmconst::dirlength;
		}
		else
		{
			::_snwprintf_s(ofn.lpstrFile, nssmconst::pathlength, _TRUNCATE, L"%s", current);
			ofn.nMaxFile = nssmconst::pathlength;
		}
	}
	ofn.lpstrTitle = message_string(NSSM_GUI_BROWSE_TITLE);
	ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | flags;

	if (::GetOpenFileNameW(&ofn))
	{
		/* Directory hack. */
		if (flags & OFN_NOVALIDATE)
			strip_basename(ofn.lpstrFile);
		::SendMessageW(window, WM_SETTEXT, 0, (LPARAM)ofn.lpstrFile);
	}
	if (ofn.lpstrFilter)
		HeapFree(GetProcessHeap(), 0, (void*)ofn.lpstrFilter);
	if (ofn.lpstrFile)
		HeapFree(GetProcessHeap(), 0, ofn.lpstrFile);
}

intptr_t CALLBACK tab_dlg(HWND tab, uint32_t message, WPARAM w, LPARAM l)
{
	switch (message)
	{
	case WM_INITDIALOG:
		return 1;

	/* Button was pressed or control was controlled. */
	case WM_COMMAND:
		HWND dlg;
		wchar_t buffer[nssmconst::pathlength];
		uint8_t enabled;

		switch (winapi::LoWord(w))
		{
		/* Browse for application. */
		case IDC_BROWSE:
			dlg = ::GetDlgItem(tab, IDC_PATH);
			::GetDlgItemTextW(tab, IDC_PATH, buffer, std::size(buffer));
			browse(dlg, buffer, OFN_FILEMUSTEXIST, NSSM_GUI_BROWSE_FILTER_APPLICATIONS, NSSM_GUI_BROWSE_FILTER_ALL_FILES, 0);
			/* Fill in startup directory if it wasn't already specified. */
			::GetDlgItemTextW(tab, IDC_DIR, buffer, std::size(buffer));
			if (!buffer[0])
			{
				::GetDlgItemTextW(tab, IDC_PATH, buffer, std::size(buffer));
				strip_basename(buffer);
				::SetDlgItemTextW(tab, IDC_DIR, buffer);
			}
			break;

		/* Browse for startup directory. */
		case IDC_BROWSE_DIR:
			dlg = ::GetDlgItem(tab, IDC_DIR);
			::GetDlgItemTextW(tab, IDC_DIR, buffer, std::size(buffer));
			browse(dlg, buffer, OFN_NOVALIDATE, NSSM_GUI_BROWSE_FILTER_DIRECTORIES, 0);
			break;

		/* Log on. */
		case IDC_LOCALSYSTEM:
			set_logon_enabled(1, 0);
			break;

		case IDC_VIRTUAL_SERVICE:
			set_logon_enabled(0, 0);
			break;

		case IDC_ACCOUNT:
			set_logon_enabled(0, 1);
			break;

		/* Affinity. */
		case IDC_AFFINITY_ALL:
			if (::SendDlgItemMessageW(tab, winapi::LoWord(w), BM_GETCHECK, 0, 0) & winapi::buttonstate::checked)
				enabled = 0;
			else
				enabled = 1;
			set_affinity_enabled(enabled);
			break;

		/* Shutdown methods. */
		case IDC_METHOD_CONSOLE:
			set_timeout_enabled(winapi::LoWord(w), IDC_KILL_CONSOLE);
			break;

		case IDC_METHOD_WINDOW:
			set_timeout_enabled(winapi::LoWord(w), IDC_KILL_WINDOW);
			break;

		case IDC_METHOD_THREADS:
			set_timeout_enabled(winapi::LoWord(w), IDC_KILL_THREADS);
			break;

		/* Browse for stdin. */
		case IDC_BROWSE_STDIN:
			dlg = ::GetDlgItem(tab, IDC_STDIN);
			::GetDlgItemTextW(tab, IDC_STDIN, buffer, std::size(buffer));
			browse(dlg, buffer, 0, NSSM_GUI_BROWSE_FILTER_ALL_FILES, 0);
			break;

		/* Browse for stdout. */
		case IDC_BROWSE_STDOUT:
			dlg = ::GetDlgItem(tab, IDC_STDOUT);
			::GetDlgItemTextW(tab, IDC_STDOUT, buffer, std::size(buffer));
			browse(dlg, buffer, 0, NSSM_GUI_BROWSE_FILTER_ALL_FILES, 0);
			/* Fill in stderr if it wasn't already specified. */
			::GetDlgItemTextW(tab, IDC_STDERR, buffer, std::size(buffer));
			if (!buffer[0])
			{
				::GetDlgItemTextW(tab, IDC_STDOUT, buffer, std::size(buffer));
				::SetDlgItemTextW(tab, IDC_STDERR, buffer);
			}
			break;

		/* Browse for stderr. */
		case IDC_BROWSE_STDERR:
			dlg = ::GetDlgItem(tab, IDC_STDERR);
			::GetDlgItemTextW(tab, IDC_STDERR, buffer, std::size(buffer));
			browse(dlg, buffer, 0, NSSM_GUI_BROWSE_FILTER_ALL_FILES, 0);
			break;

		/* Rotation. */
		case IDC_ROTATE:
			if (::SendDlgItemMessageW(tab, winapi::LoWord(w), BM_GETCHECK, 0, 0) & winapi::buttonstate::checked)
				enabled = 1;
			else
				enabled = 0;
			set_rotation_enabled(enabled);
			break;

		/* Hook event. */
		case IDC_HOOK_EVENT:
			if (winapi::HiWord(w) == CBN_SELCHANGE)
				set_hook_tab((int32_t)::SendMessageW(::GetDlgItem(tab, IDC_HOOK_EVENT), CB_GETCURSEL, 0, 0), 0, false);
			break;

		/* Hook action. */
		case IDC_HOOK_ACTION:
			if (winapi::HiWord(w) == CBN_SELCHANGE)
				set_hook_tab((int32_t)::SendMessageW(::GetDlgItem(tab, IDC_HOOK_EVENT), CB_GETCURSEL, 0, 0), (int32_t)::SendMessageW(::GetDlgItem(tab, IDC_HOOK_ACTION), CB_GETCURSEL, 0, 0), false);
			break;

		/* Browse for hook. */
		case IDC_BROWSE_HOOK:
			dlg = ::GetDlgItem(tab, IDC_HOOK);
			::GetDlgItemTextW(tab, IDC_HOOK, buffer, std::size(buffer));
			browse(dlg, L"", OFN_FILEMUSTEXIST, NSSM_GUI_BROWSE_FILTER_ALL_FILES, 0);
			break;

		/* Hook. */
		case IDC_HOOK:
			set_hook_tab((int32_t)::SendMessageW(::GetDlgItem(tab, IDC_HOOK_EVENT), CB_GETCURSEL, 0, 0), (int32_t)::SendMessageW(::GetDlgItem(tab, IDC_HOOK_ACTION), CB_GETCURSEL, 0, 0), true);
			break;
		}
		return 1;
	}

	return 0;
}

/* Install/remove dialogue callback */
intptr_t CALLBACK nssm_dlg(HWND window, uint32_t message, WPARAM w, LPARAM l)
{
	nssm_service_t* service;

	switch (message)
	{
	/* Creating the dialogue */
	case WM_INITDIALOG:
		service = (nssm_service_t*)l;

		::SetFocus(::GetDlgItem(window, IDC_NAME));

		HWND tabs;
		HWND combo;
		HWND list;
		int32_t i, n;
		tabs = ::GetDlgItem(window, IDC_TAB1);
		if (!tabs)
			return 0;

		/* Set up tabs. */
		TCITEMW tab;
		ZeroMemory(&tab, sizeof(tab));
		tab.mask = winapi::tcitemmask::text;

		selected_tab = 0;

		/* Application tab. */
		if (service->native)
			tab.pszText = message_string(NSSM_GUI_TAB_NATIVE);
		else
			tab.pszText = message_string(NSSM_GUI_TAB_APPLICATION);
		tab.cchTextMax = (int32_t)::wcslen(tab.pszText);
		::SendMessageW(tabs, TCM_INSERTITEMW, NSSM_TAB_APPLICATION, (LPARAM)&tab);
		if (service->native)
		{
			tablist[NSSM_TAB_APPLICATION] = dialog(util::MakeEnumResourcel(IDD_NATIVE), window, tab_dlg);
			::EnableWindow(tablist[NSSM_TAB_APPLICATION], 0);
			::EnableWindow(::GetDlgItem(tablist[NSSM_TAB_APPLICATION], IDC_PATH), 0);
		}
		else
			tablist[NSSM_TAB_APPLICATION] = dialog(util::MakeEnumResourcel(IDD_APPLICATION), window, tab_dlg);
		::ShowWindow(tablist[NSSM_TAB_APPLICATION], winapi::wndshowstate::show);

		/* Details tab. */
		tab.pszText = message_string(NSSM_GUI_TAB_DETAILS);
		tab.cchTextMax = (int32_t)::wcslen(tab.pszText);
		::SendMessageW(tabs, TCM_INSERTITEMW, NSSM_TAB_DETAILS, (LPARAM)&tab);
		tablist[NSSM_TAB_DETAILS] = dialog(util::MakeEnumResourcel(IDD_DETAILS), window, tab_dlg);
		::ShowWindow(tablist[NSSM_TAB_DETAILS], winapi::wndshowstate::hide);

		/* Set defaults. */
		combo = ::GetDlgItem(tablist[NSSM_TAB_DETAILS], IDC_STARTUP);
		::SendMessageW(combo, CB_INSERTSTRING, startup::automatic, (LPARAM)message_string(NSSM_GUI_STARTUP_AUTOMATIC));
		::SendMessageW(combo, CB_INSERTSTRING, startup::delayed, (LPARAM)message_string(NSSM_GUI_STARTUP_DELAYED));
		::SendMessageW(combo, CB_INSERTSTRING, startup::manual, (LPARAM)message_string(NSSM_GUI_STARTUP_MANUAL));
		::SendMessageW(combo, CB_INSERTSTRING, startup::disabled, (LPARAM)message_string(NSSM_GUI_STARTUP_DISABLED));
		::SendMessageW(combo, CB_SETCURSEL, startup::automatic, 0);

		/* Logon tab. */
		tab.pszText = message_string(NSSM_GUI_TAB_LOGON);
		tab.cchTextMax = (int32_t)::wcslen(tab.pszText);
		::SendMessageW(tabs, TCM_INSERTITEMW, NSSM_TAB_LOGON, (LPARAM)&tab);
		tablist[NSSM_TAB_LOGON] = dialog(util::MakeEnumResourcel(IDD_LOGON), window, tab_dlg);
		::ShowWindow(tablist[NSSM_TAB_LOGON], winapi::wndshowstate::hide);

		/* Set defaults. */
		::CheckRadioButton(tablist[NSSM_TAB_LOGON], IDC_LOCALSYSTEM, IDC_ACCOUNT, IDC_LOCALSYSTEM);
		set_logon_enabled(1, 0);

		/* Dependencies tab. */
		tab.pszText = message_string(NSSM_GUI_TAB_DEPENDENCIES);
		tab.cchTextMax = (int32_t)::wcslen(tab.pszText);
		::SendMessageW(tabs, TCM_INSERTITEMW, NSSM_TAB_DEPENDENCIES, (LPARAM)&tab);
		tablist[NSSM_TAB_DEPENDENCIES] = dialog(util::MakeEnumResourcel(IDD_DEPENDENCIES), window, tab_dlg);
		::ShowWindow(tablist[NSSM_TAB_DEPENDENCIES], winapi::wndshowstate::hide);

		/* Remaining tabs are only for services we manage. */
		if (service->native)
			return 1;

		/* Process tab. */
		tab.pszText = message_string(NSSM_GUI_TAB_PROCESS);
		tab.cchTextMax = (int32_t)::wcslen(tab.pszText);
		::SendMessageW(tabs, TCM_INSERTITEMW, NSSM_TAB_PROCESS, (LPARAM)&tab);
		tablist[NSSM_TAB_PROCESS] = dialog(util::MakeEnumResourcel(IDD_PROCESS), window, tab_dlg);
		::ShowWindow(tablist[NSSM_TAB_PROCESS], winapi::wndshowstate::hide);

		/* Set defaults. */
		combo = ::GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_PRIORITY);
		::SendMessageW(combo, CB_INSERTSTRING, priority::realtime, (LPARAM)message_string(NSSM_GUI_REALTIME_PRIORITY_CLASS));
		::SendMessageW(combo, CB_INSERTSTRING, priority::high, (LPARAM)message_string(NSSM_GUI_HIGH_PRIORITY_CLASS));
		::SendMessageW(combo, CB_INSERTSTRING, priority::abovenormal, (LPARAM)message_string(NSSM_GUI_ABOVE_NORMAL_PRIORITY_CLASS));
		::SendMessageW(combo, CB_INSERTSTRING, priority::normal, (LPARAM)message_string(NSSM_GUI_NORMAL_PRIORITY_CLASS));
		::SendMessageW(combo, CB_INSERTSTRING, priority::belownormal, (LPARAM)message_string(NSSM_GUI_BELOW_NORMAL_PRIORITY_CLASS));
		::SendMessageW(combo, CB_INSERTSTRING, priority::idle, (LPARAM)message_string(NSSM_GUI_IDLE_PRIORITY_CLASS));
		::SendMessageW(combo, CB_SETCURSEL, priority::normal, 0);

		::SendDlgItemMessageW(tablist[NSSM_TAB_PROCESS], IDC_CONSOLE, BM_SETCHECK, winapi::buttonstate::checked, 0);

		list = ::GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY);
		n = num_cpus();
		::SendMessageW(list, LB_SETCOLUMNWIDTH, 16, 0);
		for (i = 0; i < n; i++)
		{
			wchar_t buffer[3];
			::_snwprintf_s(buffer, std::size(buffer), _TRUNCATE, L"%d", i);
			::SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)buffer);
		}

		/*
        Size to fit.
        The box is high enough for four rows.  It is wide enough for eight
        columns without scrolling.  With scrollbars it shrinks to two rows.
        Note that the above only holds if we set the column width BEFORE
        adding the strings.
      */
		if (n < 32)
		{
			int32_t columns = (n - 1) / 4;
			RECT rect;
			::GetWindowRect(list, &rect);
			int32_t width = rect.right - rect.left;
			width -= (7 - columns) * 16;
			int32_t height = rect.bottom - rect.top;
			if (n < 4)
				height -= (int32_t)::SendMessageW(list, LB_GETITEMHEIGHT, 0, 0) * (4 - n);
			::SetWindowPos(list, 0, 0, 0, width, height, winapi::wndposflag::nomove | winapi::wndposflag::noownerzorder);
		}
		::SendMessageW(list, LB_SELITEMRANGE, 1, winapi::MakeLparam(0, n));

		::SendDlgItemMessageW(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY_ALL, BM_SETCHECK, winapi::buttonstate::checked, 0);
		set_affinity_enabled(0);

		/* Shutdown tab. */
		tab.pszText = message_string(NSSM_GUI_TAB_SHUTDOWN);
		tab.cchTextMax = (int32_t)::wcslen(tab.pszText);
		::SendMessageW(tabs, TCM_INSERTITEMW, NSSM_TAB_SHUTDOWN, (LPARAM)&tab);
		tablist[NSSM_TAB_SHUTDOWN] = dialog(util::MakeEnumResourcel(IDD_SHUTDOWN), window, tab_dlg);
		::ShowWindow(tablist[NSSM_TAB_SHUTDOWN], winapi::wndshowstate::hide);

		/* Set defaults. */
		::SendDlgItemMessageW(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_CONSOLE, BM_SETCHECK, winapi::buttonstate::checked, 0);
		::SetDlgItemInt(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_CONSOLE, wait::kill_console_grace_period, 0);
		::SendDlgItemMessageW(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_WINDOW, BM_SETCHECK, winapi::buttonstate::checked, 0);
		::SetDlgItemInt(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_WINDOW, wait::kill_window_grace_period, 0);
		::SendDlgItemMessageW(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_THREADS, BM_SETCHECK, winapi::buttonstate::checked, 0);
		::SetDlgItemInt(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_THREADS, wait::kill_threads_grace_period, 0);
		::SendDlgItemMessageW(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_TERMINATE, BM_SETCHECK, winapi::buttonstate::checked, 0);
		::SendDlgItemMessageW(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_PROCESS_TREE, BM_SETCHECK, winapi::buttonstate::checked, 1);

		/* Restart tab. */
		tab.pszText = message_string(NSSM_GUI_TAB_EXIT);
		tab.cchTextMax = (int32_t)::wcslen(tab.pszText);
		::SendMessageW(tabs, TCM_INSERTITEMW, NSSM_TAB_EXIT, (LPARAM)&tab);
		tablist[NSSM_TAB_EXIT] = dialog(util::MakeEnumResourcel(IDD_APPEXIT), window, tab_dlg);
		::ShowWindow(tablist[NSSM_TAB_EXIT], winapi::wndshowstate::hide);

		/* Set defaults. */
		::SetDlgItemInt(tablist[NSSM_TAB_EXIT], IDC_THROTTLE, wait::reset_throttle_restart, 0);
		combo = ::GetDlgItem(tablist[NSSM_TAB_EXIT], IDC_APPEXIT);
		::SendMessageW(combo, CB_INSERTSTRING, exit::restart, (LPARAM)message_string(NSSM_GUI_EXIT_RESTART));
		::SendMessageW(combo, CB_INSERTSTRING, exit::ignore, (LPARAM)message_string(NSSM_GUI_EXIT_IGNORE));
		::SendMessageW(combo, CB_INSERTSTRING, exit::really, (LPARAM)message_string(NSSM_GUI_EXIT_REALLY));
		::SendMessageW(combo, CB_INSERTSTRING, exit::unclean, (LPARAM)message_string(NSSM_GUI_EXIT_UNCLEAN));
		::SendMessageW(combo, CB_SETCURSEL, exit::restart, 0);
		::SetDlgItemInt(tablist[NSSM_TAB_EXIT], IDC_RESTART_DELAY, 0, 0);

		/* I/O tab. */
		tab.pszText = message_string(NSSM_GUI_TAB_IO);
		tab.cchTextMax = (int32_t)::wcslen(tab.pszText) + 1;
		::SendMessageW(tabs, TCM_INSERTITEMW, NSSM_TAB_IO, (LPARAM)&tab);
		tablist[NSSM_TAB_IO] = dialog(util::MakeEnumResourcel(IDD_IO), window, tab_dlg);
		::ShowWindow(tablist[NSSM_TAB_IO], winapi::wndshowstate::hide);

		/* Set defaults. */
		::SendDlgItemMessageW(tablist[NSSM_TAB_IO], IDC_TIMESTAMP, BM_SETCHECK, winapi::buttonstate::unchecked, 0);

		/* Rotation tab. */
		tab.pszText = message_string(NSSM_GUI_TAB_ROTATION);
		tab.cchTextMax = (int32_t)::wcslen(tab.pszText) + 1;
		::SendMessageW(tabs, TCM_INSERTITEMW, NSSM_TAB_ROTATION, (LPARAM)&tab);
		tablist[NSSM_TAB_ROTATION] = dialog(util::MakeEnumResourcel(IDD_ROTATION), window, tab_dlg);
		::ShowWindow(tablist[NSSM_TAB_ROTATION], winapi::wndshowstate::hide);

		/* Set defaults. */
		::SendDlgItemMessageW(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_ONLINE, BM_SETCHECK, winapi::buttonstate::unchecked, 0);
		::SetDlgItemInt(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_SECONDS, 0, 0);
		::SetDlgItemInt(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_BYTES_LOW, 0, 0);
		set_rotation_enabled(0);

		/* Environment tab. */
		tab.pszText = message_string(NSSM_GUI_TAB_ENVIRONMENT);
		tab.cchTextMax = (int32_t)::wcslen(tab.pszText) + 1;
		::SendMessageW(tabs, TCM_INSERTITEMW, NSSM_TAB_ENVIRONMENT, (LPARAM)&tab);
		tablist[NSSM_TAB_ENVIRONMENT] = dialog(util::MakeEnumResourcel(IDD_ENVIRONMENT), window, tab_dlg);
		::ShowWindow(tablist[NSSM_TAB_ENVIRONMENT], winapi::wndshowstate::hide);

		/* Hooks tab. */
		tab.pszText = message_string(NSSM_GUI_TAB_HOOKS);
		tab.cchTextMax = (int32_t)::wcslen(tab.pszText) + 1;
		::SendMessageW(tabs, TCM_INSERTITEMW, NSSM_TAB_HOOKS, (LPARAM)&tab);
		tablist[NSSM_TAB_HOOKS] = dialog(util::MakeEnumResourcel(IDD_HOOKS), window, tab_dlg);
		::ShowWindow(tablist[NSSM_TAB_HOOKS], winapi::wndshowstate::hide);

		/* Set defaults. */
		combo = ::GetDlgItem(tablist[NSSM_TAB_HOOKS], IDC_HOOK_EVENT);
		::SendMessageW(combo, CB_INSERTSTRING, -1, (LPARAM)message_string(NSSM_GUI_HOOK_EVENT_START));
		::SendMessageW(combo, CB_INSERTSTRING, -1, (LPARAM)message_string(NSSM_GUI_HOOK_EVENT_STOP));
		::SendMessageW(combo, CB_INSERTSTRING, -1, (LPARAM)message_string(NSSM_GUI_HOOK_EVENT_EXIT));
		::SendMessageW(combo, CB_INSERTSTRING, -1, (LPARAM)message_string(NSSM_GUI_HOOK_EVENT_POWER));
		::SendMessageW(combo, CB_INSERTSTRING, -1, (LPARAM)message_string(NSSM_GUI_HOOK_EVENT_ROTATE));
		::SendDlgItemMessageW(tablist[NSSM_TAB_HOOKS], IDC_REDIRECT_HOOK, BM_SETCHECK, winapi::buttonstate::unchecked, 0);
		if (::wcslen(service->name))
		{
			wchar_t hook_name[hookconst::namelength];
			wchar_t cmd[CMD_LENGTH];
			for (i = 0; hook_event_strings[i]; i++)
			{
				const wchar_t* hook_event = hook_event_strings[i];
				int32_t j;
				for (j = 0; hook_action_strings[j]; j++)
				{
					const wchar_t* hook_action = hook_action_strings[j];
					if (!valid_hook_name(hook_event, hook_action, true))
						continue;
					if (get_hook(service->name, hook_event, hook_action, cmd, sizeof(cmd)))
						continue;
					if (hook_env(hook_event, hook_action, hook_name, std::size(hook_name)) < 0)
						continue;
					::SetEnvironmentVariableW(hook_name, cmd);
				}
			}
		}
		set_hook_tab(0, 0, false);

		return 1;

	/* Tab change. */
	case WM_NOTIFY:
		NMHDR* notification;

		notification = (NMHDR*)l;
		switch (notification->code)
		{
		case TCN_SELCHANGE:
			HWND tabs;
			int32_t selection;

			tabs = ::GetDlgItem(window, IDC_TAB1);
			if (!tabs)
				return 0;

			selection = (int32_t)::SendMessageW(tabs, TCM_GETCURSEL, 0, 0);
			if (selection != selected_tab)
			{
				::ShowWindow(tablist[selected_tab], winapi::wndshowstate::hide);
				::ShowWindow(tablist[selection], winapi::wndshowstate::showdefault);
				::SetFocus(::GetDlgItem(window, IDOK));
				selected_tab = selection;
			}
			return 1;
		}

		return 0;

	/* Button was pressed or control was controlled */
	case WM_COMMAND:
		switch (winapi::LoWord(w))
		{
		/* OK button */
		case IDOK:
			if ((int32_t)::GetWindowLongPtrW(window, winapi::winlongindex::userdata) == IDD_EDIT)
			{
				if (!edit(window, (nssm_service_t*)::GetWindowLongPtrW(window, winapi::winlongindex::user)))
					::PostQuitMessage(0);
			}
			else if (!install(window))
				::PostQuitMessage(0);
			break;

		/* Cancel button */
		case IDCANCEL:
			::DestroyWindow(window);
			break;

		/* Remove button */
		case IDC_REMOVE:
			if (!remove(window))
				::PostQuitMessage(0);
			break;
		}
		return 1;

	/* Window closing */
	case WM_CLOSE:
		::DestroyWindow(window);
		return 0;
	case WM_DESTROY:
		::PostQuitMessage(0);
	}
	return 0;
}
