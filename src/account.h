/*******************************************************************************
 account.h - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#ifndef ACCOUNT_H
#define ACCOUNT_H

#pragma once

#include <ntsecapi.h>

/* Not really an account.  The canonical name is NT Authority\System. */
#define NSSM_LOCALSYSTEM_ACCOUNT            L"LocalSystem"
/* Other well-known accounts which can start a service without a password. */
#define NSSM_LOCALSERVICE_ACCOUNT           L"NT Authority\\LocalService"
#define NSSM_NETWORKSERVICE_ACCOUNT         L"NT Authority\\NetworkService"
/* Virtual service accounts. */
#define NSSM_VIRTUAL_SERVICE_ACCOUNT_DOMAIN L"NT Service"
/* This is explicitly a wide string. */
#define NSSM_LOGON_AS_SERVICE_RIGHT         L"SeServiceLogonRight"

int32_t open_lsa_policy(LSA_HANDLE*);
int32_t username_sid(const wchar_t*, SID**, LSA_HANDLE*);
int32_t username_sid(const wchar_t*, SID**);
int32_t username_equiv(const wchar_t*, const wchar_t*);
int32_t canonicalise_username(const wchar_t*, wchar_t**);
int32_t is_localsystem(const wchar_t*);
wchar_t* virtual_account(const wchar_t*);
int32_t is_virtual_account(const wchar_t*, const wchar_t*);
const wchar_t* well_known_sid(SID*);
const wchar_t* well_known_username(const wchar_t*);
int32_t grant_logon_as_service(const wchar_t*);

#endif
