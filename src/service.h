/*******************************************************************************
 service.h - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#pragma once

#ifndef SERVICE_H
#define SERVICE_H

/*
  MSDN says the commandline in ::CreateProcessW() is limited to 32768 characters
  and the application name to MAX_PATH.
  A service name and service display name are limited to 256 characters.
  A registry key is limited to 255 characters.
  A registry value is limited to 16383 characters.
  Therefore we limit the service name to accommodate the path under HKLM.
*/
#define EXE_LENGTH                     nssmconst::pathlength
#define CMD_LENGTH                     32768
#define KEY_LENGTH                     255
#define VALUE_LENGTH                   16383
#define SERVICE_NAME_LENGTH            256

#define ACTION_LEN                     16

#define NSSM_KERNEL_DRIVER             L"SERVICE_KERNEL_DRIVER"
#define NSSM_FILE_SYSTEM_DRIVER        L"SERVICE_FILE_SYSTEM_DRIVER"
#define NSSM_WIN32_OWN_PROCESS         L"SERVICE_WIN32_OWN_PROCESS"
#define NSSM_WIN32_SHARE_PROCESS       L"SERVICE_WIN32_SHARE_PROCESS"
#define NSSM_INTERACTIVE_PROCESS       L"SERVICE_INTERACTIVE_PROCESS"
#define NSSM_SHARE_INTERACTIVE_PROCESS NSSM_WIN32_SHARE_PROCESS L"|" NSSM_INTERACTIVE_PROCESS
#define NSSM_UNKNOWN                   L"?"

#define NSSM_ROTATE_OFFLINE            0
#define NSSM_ROTATE_ONLINE             1
#define NSSM_ROTATE_ONLINE_ASAP        2

struct nssm_service_t
{
	bool native;
	wchar_t name[SERVICE_NAME_LENGTH];
	wchar_t displayname[SERVICE_NAME_LENGTH];
	wchar_t description[VALUE_LENGTH];
	uint32_t startup;
	wchar_t* username;
	size_t usernamelen;
	wchar_t* password;
	size_t passwordlen;
	uint32_t type;
	wchar_t image[nssmconst::pathlength];
	wchar_t exe[EXE_LENGTH];
	wchar_t flags[VALUE_LENGTH];
	wchar_t dir[nssmconst::dirlength];
	wchar_t* env;
	int64_t affinity;
	wchar_t* dependencies;
	uint32_t dependencieslen;
	uint32_t envlen;
	wchar_t* env_extra;
	uint32_t env_extralen;
	uint32_t priority;
	uint32_t no_console;
	wchar_t stdin_path[nssmconst::pathlength];
	uint32_t stdin_sharing;
	uint32_t stdin_disposition;
	uint32_t stdin_flags;
	wchar_t stdout_path[nssmconst::pathlength];
	uint32_t stdout_sharing;
	uint32_t stdout_disposition;
	uint32_t stdout_flags;
	bool use_stdout_pipe;
	HANDLE stdout_si;
	HANDLE stdout_pipe;
	HANDLE stdout_thread;
	uint32_t stdout_tid;
	wchar_t stderr_path[nssmconst::pathlength];
	uint32_t stderr_sharing;
	uint32_t stderr_disposition;
	uint32_t stderr_flags;
	bool use_stderr_pipe;
	HANDLE stderr_si;
	HANDLE stderr_pipe;
	HANDLE stderr_thread;
	uint32_t stderr_tid;
	bool hook_share_output_handles;
	bool rotate_files;
	bool timestamp_log;
	bool stdout_copy_and_truncate;
	bool stderr_copy_and_truncate;
	uint32_t rotate_stdout_online;
	uint32_t rotate_stderr_online;
	uint32_t rotate_seconds;
	uint32_t rotate_bytes_low;
	uint32_t rotate_bytes_high;
	uint32_t rotate_delay;
	uint32_t default_exit_action;
	uint32_t restart_delay;
	uint32_t throttle_delay;
	uint32_t stop_method;
	uint32_t kill_console_delay;
	uint32_t kill_window_delay;
	uint32_t kill_threads_delay;
	bool kill_process_tree;
	SC_HANDLE handle;
	SERVICE_STATUS status;
	SERVICE_STATUS_HANDLE status_handle;
	HANDLE process_handle;
	uint32_t pid;
	HANDLE wait_handle;
	uint32_t exitcode;
	bool stopping;
	bool allow_restart;
	uint32_t throttle;
	CRITICAL_SECTION throttle_section;
	bool throttle_section_initialised;
	CRITICAL_SECTION hook_section;
	bool hook_section_initialised;
	CONDITION_VARIABLE throttle_condition;
	HANDLE throttle_timer;
	LARGE_INTEGER throttle_duetime;
	FILETIME nssm_creation_time;
	FILETIME creation_time;
	FILETIME exit_time;
	wchar_t* initial_env;
	uint32_t last_control;
	uint32_t start_requested_count;
	uint32_t start_count;
	uint32_t exit_count;
};

void WINAPI service_main(uint32_t, wchar_t**);
wchar_t* service_control_text(uint32_t);
wchar_t* service_status_text(uint32_t);
void log_service_control(wchar_t*, uint32_t, bool);
uint32_t WINAPI service_control_handler(uint32_t, uint32_t, void*, void*);

int32_t affinity_mask_to_string(int64_t, wchar_t**);
int32_t affinity_string_to_mask(wchar_t*, int64_t*);
uint32_t priority_mask();
int32_t priority_constant_to_index(uint32_t);
uint32_t priority_index_to_constant(int32_t);

nssm_service_t* alloc_nssm_service();
void set_nssm_service_defaults(nssm_service_t*);
void cleanup_nssm_service(nssm_service_t*);
SC_HANDLE open_service_manager(uint32_t);
SC_HANDLE open_service(SC_HANDLE, wchar_t*, uint32_t, wchar_t*, uint32_t);
QUERY_SERVICE_CONFIGW* query_service_config(const wchar_t*, SC_HANDLE);
int32_t append_to_dependencies(wchar_t*, uint32_t, wchar_t*, wchar_t**, uint32_t*, int32_t);
int32_t remove_from_dependencies(wchar_t*, uint32_t, wchar_t*, wchar_t**, uint32_t*, int32_t);
int32_t set_service_dependencies(const wchar_t*, SC_HANDLE, wchar_t*);
int32_t get_service_dependencies(const wchar_t*, SC_HANDLE, wchar_t**, uint32_t*, int32_t);
int32_t get_service_dependencies(const wchar_t*, SC_HANDLE, wchar_t**, uint32_t*);
int32_t set_service_description(const wchar_t*, SC_HANDLE, wchar_t*);
int32_t get_service_description(const wchar_t*, SC_HANDLE, uint32_t, wchar_t*);
int32_t get_service_startup(const wchar_t*, SC_HANDLE, const QUERY_SERVICE_CONFIGW*, uint32_t*);
int32_t get_service_username(const wchar_t*, const QUERY_SERVICE_CONFIGW*, wchar_t**, size_t*);
void set_service_environment(nssm_service_t*);
void unset_service_environment(nssm_service_t*);
int32_t pre_install_service(int32_t, wchar_t**);
int32_t pre_remove_service(int32_t, wchar_t**);
int32_t pre_edit_service(int32_t, wchar_t**);
int32_t install_service(nssm_service_t*);
int32_t remove_service(nssm_service_t*);
int32_t edit_service(nssm_service_t*, bool);
int32_t control_service(uint32_t, int32_t, wchar_t**, bool);
int32_t control_service(uint32_t, int32_t, wchar_t**);
void set_service_recovery(nssm_service_t*);
int32_t monitor_service(nssm_service_t*);
int32_t start_service(nssm_service_t*);
int32_t stop_service(nssm_service_t*, uint32_t, bool, bool);
void CALLBACK end_service(void*, uint8_t);
void throttle_restart(nssm_service_t*);
int32_t await_single_handle(SERVICE_STATUS_HANDLE, SERVICE_STATUS*, HANDLE, wchar_t*, wchar_t*, uint32_t);
int32_t list_nssm_services(int32_t, wchar_t**);
int32_t service_process_tree(int32_t, wchar_t**);

#endif
