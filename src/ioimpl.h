/*******************************************************************************
 ioimpl.h - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#pragma once

#ifndef IO_H
#define IO_H

#define NSSM_STDIN_SHARING      FILE_SHARE_WRITE
#define NSSM_STDIN_DISPOSITION  OPEN_EXISTING
#define NSSM_STDIN_FLAGS        FILE_ATTRIBUTE_NORMAL
#define NSSM_STDOUT_SHARING     (FILE_SHARE_READ | FILE_SHARE_WRITE)
#define NSSM_STDOUT_DISPOSITION OPEN_ALWAYS
#define NSSM_STDOUT_FLAGS       FILE_ATTRIBUTE_NORMAL
#define NSSM_STDERR_SHARING     (FILE_SHARE_READ | FILE_SHARE_WRITE)
#define NSSM_STDERR_DISPOSITION OPEN_ALWAYS
#define NSSM_STDERR_FLAGS       FILE_ATTRIBUTE_NORMAL

typedef struct
{
	wchar_t* service_name;
	wchar_t* path;
	uint32_t sharing;
	uint32_t disposition;
	uint32_t flags;
	HANDLE read_handle;
	HANDLE write_handle;
	int64_t size;
	uint32_t* tid_ptr;
	uint32_t* rotate_online;
	bool timestamp_log;
	int64_t line_length;
	bool copy_and_truncate;
	uint32_t rotate_delay;
} logger_t;

void close_handle(HANDLE*, HANDLE*);
void close_handle(HANDLE*);
int32_t get_createfile_parameters(HKEY, wchar_t*, wchar_t*, uint32_t*, uint32_t, uint32_t*, uint32_t, uint32_t*, uint32_t, bool*);
int32_t set_createfile_parameter(HKEY, wchar_t*, wchar_t*, uint32_t);
int32_t delete_createfile_parameter(HKEY, wchar_t*, wchar_t*);
HANDLE write_to_file(wchar_t*, uint32_t, SECURITY_ATTRIBUTES*, uint32_t, uint32_t);
void rotate_file(wchar_t*, wchar_t*, uint32_t, uint32_t, uint32_t, uint32_t, bool);
int32_t get_output_handles(nssm_service_t*, STARTUPINFOW*);
int32_t use_output_handles(nssm_service_t*, STARTUPINFOW*);
void close_output_handles(STARTUPINFOW*);
void cleanup_loggers(nssm_service_t*);
ULONG __stdcall log_and_rotate(void*);	// LPTHREAD_START_ROUTINE

#endif
