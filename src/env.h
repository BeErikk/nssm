/*******************************************************************************
 env.h - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#pragma once

#ifndef ENV_H
#define ENV_H

size_t environment_length(wchar_t*);
wchar_t* copy_environment_block(wchar_t*);
wchar_t* useful_environment(wchar_t*);
wchar_t* expand_environment_string(wchar_t*);
int32_t set_environment_block(wchar_t*);
int32_t clear_environment();
int32_t duplicate_environment(wchar_t*);
int32_t test_environment(wchar_t*);
void duplicate_environment_strings(wchar_t*);
wchar_t* copy_environment();
int32_t append_to_environment_block(wchar_t*, uint32_t, wchar_t*, wchar_t**, uint32_t*);
int32_t remove_from_environment_block(wchar_t*, uint32_t, wchar_t*, wchar_t**, uint32_t*);

#endif
