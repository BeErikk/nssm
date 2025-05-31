/*******************************************************************************
 utf8.h - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#pragma once

#ifndef UTF8_H
#define UTF8_H

void setup_utf8();
void unsetup_utf8();
int32_t to_utf8(const wchar_t*, char**, uint32_t*);
int32_t to_utf8(const char*, char**, uint32_t*);
int32_t to_utf16(const char*, wchar_t** utf16, uint32_t*);
int32_t to_utf16(const wchar_t*, wchar_t** utf16, uint32_t*);
int32_t from_utf8(const char*, wchar_t**, uint32_t*);
int32_t from_utf16(const wchar_t*, wchar_t**, uint32_t*);

#endif
