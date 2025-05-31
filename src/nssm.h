/*******************************************************************************
 nssm.h - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#pragma once

#ifndef NSSM_H
#define NSSM_H

/*
  MSDN says, basically, that the maximum length of a path is 260 characters,
  which is represented by the constant MAX_PATH.  Except when it isn't.

  The maximum length of a directory path is MAX_PATH - 12 because it must be
  possible to create a file in 8.3 format under any valid directory.

  Unicode versions of filesystem API functions accept paths up to 32767
  characters if the first four (wide) characters are L"\\?\" and each component
  of the path, separated by L"\", does not exceed the value of
  lpMaximumComponentLength returned by GetVolumeInformation(), which is
  probably 255.  But might not be.

  Relative paths are always limited to MAX_PATH because the L"\\?\" prefix
  is not valid for a relative path.

  Note that we don't care about the last two paragraphs because we're only
  concerned with allocating buffers big enough to store valid paths.  If the
  user tries to store invalid paths they will fit in the buffers but the
  application will fail.  The reason for the failure will end up in the
  event log and the user will realise the mistake.

  So that's that cleared up, then.
*/
//#ifdef UNICODE
//#define PATH_LENGTH 32767
//#else
//#define PATH_LENGTH MAX_PATH
//#endif
//#define DIR_LENGTH PATH_LENGTH - 12

//#define _WIN32_WINNT 0x0500

#define APSTUDIO_HIDDEN_SYMBOLS
#include <windows.h>
#include <prsht.h>
#undef APSTUDIO_HIDDEN_SYMBOLS
#include <commctrl.h>
//#include <tchar.h>
#ifndef NSSM_COMPILE_RC
#include <fcntl.h>
#include <io.h>
#include <shlwapi.h>
#include <stdarg.h>
#include <stdio.h>
#include "utf8.h"
#include "service.h"
#include "account.h"
#include "console.h"
#include "env.h"
#include "event.h"
#include "hook.h"
#include "imports.h"
#include "messages.h"
#include "process_impl.h"
#include "registry.h"
#include "settings.h"
#include "io-impl.h"
#include "gui.h"
#endif

void nssm_exit(int32_t);
int32_t str_equiv(const wchar_t*, const wchar_t*);
int32_t quote(const wchar_t*, wchar_t*, size_t);
void strip_basename(wchar_t*);
int32_t str_number(const wchar_t*, uint32_t*, wchar_t**);
int32_t str_number(const wchar_t*, uint32_t*);
int32_t num_cpus();
int32_t usage(int32_t);
const wchar_t* nssm_unquoted_imagepath();
const wchar_t* nssm_imagepath();
const wchar_t* nssm_exe();

#define NSSM L"NSSM"
#ifdef _WIN64
#define NSSM_ARCHITECTURE L"64-bit"
#else
#define NSSM_ARCHITECTURE L"32-bit"
#endif
#ifdef _DEBUG
#define NSSM_DEBUG L" debug"
#else
#define NSSM_DEBUG L""
#endif
#define NSSM_CONFIGURATION NSSM_ARCHITECTURE NSSM_DEBUG
#include "version.h"

