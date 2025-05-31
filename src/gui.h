/*******************************************************************************
 gui.h - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#pragma once

#ifndef GUI_H
#define GUI_H

#include <stdio.h>
#include <windows.h>
#include <commctrl.h>
#include "resource.h"

int32_t nssm_gui(int32_t, nssm_service_t*);
void centre_window(HWND);
int32_t configure(HWND, nssm_service_t*, nssm_service_t*);
int32_t install(HWND);
int32_t remove(HWND);
int32_t edit(HWND, nssm_service_t*);
void browse(HWND);
intptr_t CALLBACK nssm_dlg(HWND, uint32_t, WPARAM, LPARAM);

#endif
