/*******************************************************************************
 console.cpp - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#include "nssm_pch.h"
#include "common.h"

#include "console.h"

/* See if we were launched from a console window. */
bool check_console()
{
	/* If we're running in a service context there will be no console window. */
	HWND console = GetConsoleWindow();
	if (!console)
		return false;

	ULONG pid;
	if (!::GetWindowThreadProcessId(console, &pid))
		return false;

	/*
    If the process associated with the console window handle is the same as
    this process, we were not launched from an existing console.  The user
    probably double-clicked our executable.
  */
	if (GetCurrentProcessId() != pid)
		return true;

	/* We close our new console so that subsequent messages appear in a popup. */
	FreeConsole();
	return false;
}

void alloc_console(nssm_service_t* service)
{
	if (service->no_console)
		return;

	AllocConsole();
}
