/*******************************************************************************
 console.h - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#pragma once

#ifndef CONSOLE_H
#define CONSOLE_H

bool check_console();
void alloc_console(nssm_service_t*);

#endif
