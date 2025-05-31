/*******************************************************************************
 imports.h - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#pragma once

#ifndef IMPORTS_H
#define IMPORTS_H

/* Some functions don't have decorated versions. */
#ifdef UNICODE
#define QUERYFULLPROCESSIMAGENAME_EXPORT "QueryFullProcessImageNameW"
#else
#define QUERYFULLPROCESSIMAGENAME_EXPORT "QueryFullProcessImageNameA"
#endif

typedef BOOL(WINAPI* AttachConsole_ptr)(uint32_t);
typedef BOOL(WINAPI* SleepConditionVariableCS_ptr)(PCONDITION_VARIABLE, PCRITICAL_SECTION, uint32_t);
typedef BOOL(WINAPI* QueryFullProcessImageName_ptr)(HANDLE, uint32_t, PWSTR, uint32_t*);
typedef void(WINAPI* WakeConditionVariable_ptr)(PCONDITION_VARIABLE);
typedef BOOL(WINAPI* CreateWellKnownSid_ptr)(WELL_KNOWN_SID_TYPE, SID*, SID*, uint32_t*);
typedef BOOL(WINAPI* IsWellKnownSid_ptr)(SID*, WELL_KNOWN_SID_TYPE);

struct imports_t
{
	HMODULE kernel32;
	HMODULE advapi32;
	AttachConsole_ptr AttachConsole;
	SleepConditionVariableCS_ptr SleepConditionVariableCS;
	QueryFullProcessImageName_ptr QueryFullProcessImageNameW;
	WakeConditionVariable_ptr WakeConditionVariable;
	CreateWellKnownSid_ptr CreateWellKnownSid;
	IsWellKnownSid_ptr IsWellKnownSid;
};

HMODULE get_dll(const wchar_t*, uint32_t*);
FARPROC get_import(HMODULE, const char*, uint32_t*);
int32_t get_imports();
void free_imports();

#endif
