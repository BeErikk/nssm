/*******************************************************************************
 utf8.cpp - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#include "nssm_pch.h"
#include "common.h"

#include "utf8.h"

static uint32_t cp;

void setup_utf8()
{
#ifdef UNICODE
	/*
    Ensure we write in UTF-8 mode, so that non-ASCII characters don't get
    mangled.  If we were compiled in ANSI mode it won't work.
   */
	cp = GetConsoleOutputCP();
	SetConsoleOutputCP(CP_UTF8);
	_setmode(_fileno(stdout), _O_U8TEXT);
	_setmode(_fileno(stderr), _O_U8TEXT);
#endif
}

void unsetup_utf8()
{
	if (cp)
		SetConsoleOutputCP(cp);
}

/*
  Conversion functions.

  to_utf8/16() converts a string which may be either utf8 or utf16 to
  the desired format.  If no conversion is needed a new string is still
  allocated and the old string is copied.

  from_utf8/16() converts a string which IS in the specified format to
  whichever format is needed according to whether UNICODE is defined.
  It simply wraps the appropriate to_utf8/16() function.

  Therefore the caller must ALWAYS free the destination pointer after a
  successful (return code 0) call to one of these functions.

  The length pointer is optional.  Pass nullptr if you don't care about
  the length of the converted string.

  Both the destination and the length, if supplied, will be zeroed if
  no conversion was done.
*/
int32_t to_utf8(const wchar_t* utf16, char** utf8, uint32_t* utf8len)
{
	*utf8 = 0;
	if (utf8len)
		*utf8len = 0;
	int32_t size = WideCharToMultiByte(CP_UTF8, 0, utf16, -1, nullptr, 0, nullptr, nullptr);
	if (!size)
		return 1;

	*utf8 = (char*)HeapAlloc(GetProcessHeap(), 0, size);
	if (!*utf8)
		return 2;

	if (!WideCharToMultiByte(CP_UTF8, 0, utf16, -1, *utf8, size, nullptr, nullptr))
	{
		HeapFree(GetProcessHeap(), 0, *utf8);
		*utf8 = 0;
		return 3;
	}

	if (utf8len)
		*utf8len = (uint32_t)strlen(*utf8);

	return 0;
}

int32_t to_utf8(const char* ansi, char** utf8, uint32_t* utf8len)
{
	*utf8 = 0;
	if (utf8len)
		*utf8len = 0;
	size_t len = strlen(ansi);
	int32_t size = (int32_t)len + 1;

	*utf8 = (char*)HeapAlloc(GetProcessHeap(), 0, size);
	if (!*utf8)
		return 2;

	if (utf8len)
		*utf8len = (uint32_t)len;
	memmove(*utf8, ansi, size);

	return 0;
}

int32_t to_utf16(const char* utf8, wchar_t** utf16, uint32_t* utf16len)
{
	*utf16 = 0;
	if (utf16len)
		*utf16len = 0;
	int32_t size = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
	if (!size)
		return 1;

	*utf16 = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, size * sizeof(wchar_t));
	if (!*utf16)
		return 2;

	if (!MultiByteToWideChar(CP_UTF8, 0, utf8, -1, *utf16, size))
	{
		HeapFree(GetProcessHeap(), 0, *utf16);
		*utf16 = 0;
		return 3;
	}

	if (utf16len)
		*utf16len = (uint32_t)::wcslen(*utf16);

	return 0;
}

int32_t to_utf16(const wchar_t* unicode, wchar_t** utf16, uint32_t* utf16len)
{
	*utf16 = 0;
	if (utf16len)
		*utf16len = 0;
	size_t len = ::wcslen(unicode);
	int32_t size = ((int32_t)len + 1) * sizeof(wchar_t);

	*utf16 = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, size);
	if (!*utf16)
		return 2;

	if (utf16len)
		*utf16len = (uint32_t)len;
	memmove(*utf16, unicode, size);

	return 0;
}

int32_t from_utf8(const char* utf8, wchar_t** buffer, uint32_t* buflen)
{
#ifdef UNICODE
	return to_utf16(utf8, buffer, buflen);
#else
	return to_utf8(utf8, buffer, buflen);
#endif
}

int32_t from_utf16(const wchar_t* utf16, wchar_t** buffer, uint32_t* buflen)
{
#ifdef UNICODE
	return to_utf16(utf16, buffer, buflen);
#else
	return to_utf8(utf16, buffer, buflen);
#endif
}
