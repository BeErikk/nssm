/*******************************************************************************
 event.h - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#pragma once

#ifndef EVENT_H
#define EVENT_H

wchar_t* error_string(uint32_t);
wchar_t* message_string(uint32_t);
void log_event(uint16_t, uint32_t, ...);
void print_message(FILE*, uint32_t, ...);
int32_t popup_message(HWND, uint32_t, uint32_t, ...);

#endif
