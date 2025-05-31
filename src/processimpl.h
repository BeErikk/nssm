/*******************************************************************************
 processimpl.h - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#pragma once

#ifndef PROCESS_H
#define PROCESS_H

#include <psapi.h>
#include <tlhelp32.h>

struct kill_t
{
	const wchar_t* name;
	HANDLE process_handle;
	uint32_t depth;
	uint32_t pid;
	uint32_t exitcode;
	uint32_t stop_method;
	uint32_t kill_console_delay;
	uint32_t kill_window_delay;
	uint32_t kill_threads_delay;
	SERVICE_STATUS_HANDLE status_handle;
	SERVICE_STATUS* status;
	FILETIME creation_time;
	FILETIME exit_time;
	int32_t signalled;
};

typedef int32_t (*walk_function_t)(nssm_service_t*, kill_t*);

HANDLE get_debug_token();
void service_kill_t(nssm_service_t*, kill_t*);
int32_t get_process_creation_time(HANDLE, FILETIME*);
int32_t get_process_exit_time(HANDLE, FILETIME*);
int32_t check_parent(kill_t*, PROCESSENTRY32*, uint32_t);
int32_t CALLBACK kill_window(HWND, LPARAM);
int32_t kill_threads(nssm_service_t*, kill_t*);
int32_t kill_threads(kill_t*);
int32_t kill_console(nssm_service_t*, kill_t*);
int32_t kill_console(kill_t*);
int32_t kill_process(nssm_service_t*, kill_t*);
int32_t kill_process(kill_t*);
int32_t print_process(nssm_service_t*, kill_t*);
int32_t print_process(kill_t*);
void walk_process_tree(nssm_service_t*, walk_function_t, kill_t*, uint32_t);
void kill_process_tree(kill_t*, uint32_t);

#endif
