/*******************************************************************************
 account.cpp - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#include "nssm_pch.h"
#include "common.h"

#include "account.h"

//#include <sddl.h>

//#ifndef STATUS_SUCCESS
//#define STATUS_SUCCESS ERROR_SUCCESS
//#endif

//extern imports_t imports;

/* Open Policy object. */
int32_t open_lsa_policy(LSA_HANDLE* policy)
{
	LSA_OBJECT_ATTRIBUTES attributes;
	ZeroMemory(&attributes, sizeof(attributes));

	NTSTATUS status = LsaOpenPolicy(nullptr, &attributes, POLICY_ALL_ACCESS, policy);
	if (status != STATUS_SUCCESS)
	{
		print_message(stderr, NSSM_MESSAGE_LSAOPENPOLICY_FAILED, error_string(LsaNtStatusToWinError(status)));
		return 1;
	}

	return 0;
}

/* Look up SID for an account. */
int32_t username_sid(const wchar_t* username, SID** sid, LSA_HANDLE* policy)
{
	LSA_HANDLE handle;
	if (!policy)
	{
		policy = &handle;
		if (open_lsa_policy(policy))
			return 1;
	}

	/*
    LsaLookupNames() can't look up .\username but can look up
    %COMPUTERNAME%\username.  ChangeServiceConfig() writes .\username to the
    registry when %COMPUTERNAME%\username is a passed as a parameter.  We
    need to preserve .\username when calling ChangeServiceConfig() without
    changing the username, but expand to %COMPUTERNAME%\username when calling
    LsaLookupNames().
  */
	wchar_t* expanded;
	ULONG expandedlen;
	if (_wcsnicmp(L".\\", username, 2) != 0)
	{
		expandedlen = (uint32_t)(::wcslen(username) + 1) * sizeof(wchar_t);
		expanded = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, expandedlen);
		if (!expanded)
		{
			print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"expanded", L"username_sid");
			if (policy == &handle)
				LsaClose(handle);
			return 2;
		}
		memmove(expanded, username, expandedlen);
	}
	else
	{
		wchar_t computername[MAX_COMPUTERNAME_LENGTH + 1];
		expandedlen = std::size(computername);
		::GetComputerNameW(computername, &expandedlen);
		expandedlen += (uint32_t)::wcslen(username);

		expanded = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, expandedlen * sizeof(wchar_t));
		if (!expanded)
		{
			print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"expanded", L"username_sid");
			if (policy == &handle)
				LsaClose(handle);
			return 2;
		}
		::_snwprintf_s(expanded, expandedlen, _TRUNCATE, L"%s\\%s", computername, username + 2);
	}

	LSA_UNICODE_STRING lsa_username;
	int32_t ret = to_utf16(expanded, &lsa_username.Buffer, (uint32_t*)&lsa_username.Length);
	HeapFree(GetProcessHeap(), 0, expanded);
	if (ret)
	{
		if (policy == &handle)
			LsaClose(handle);
		print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"LSA_UNICODE_STRING", L"username_sid()");
		return 4;
	}
	lsa_username.Length *= sizeof(wchar_t);
	lsa_username.MaximumLength = lsa_username.Length + sizeof(wchar_t);

	LSA_REFERENCED_DOMAIN_LIST* translated_domains;
	LSA_TRANSLATED_SID* translated_sid;
	NTSTATUS status = LsaLookupNames(*policy, 1, &lsa_username, &translated_domains, &translated_sid);
	HeapFree(GetProcessHeap(), 0, lsa_username.Buffer);
	if (policy == &handle)
		LsaClose(handle);
	if (status != STATUS_SUCCESS)
	{
		LsaFreeMemory(translated_domains);
		LsaFreeMemory(translated_sid);
		print_message(stderr, NSSM_MESSAGE_LSALOOKUPNAMES_FAILED, username, error_string(LsaNtStatusToWinError(status)));
		return 5;
	}

	if (translated_sid->Use != SidTypeUser && translated_sid->Use != SidTypeWellKnownGroup)
	{
		if (translated_sid->Use != SidTypeUnknown || _wcsnicmp(NSSM_VIRTUAL_SERVICE_ACCOUNT_DOMAIN L"\\", username, ::wcslen(NSSM_VIRTUAL_SERVICE_ACCOUNT_DOMAIN) + 1) != 0)
		{
			LsaFreeMemory(translated_domains);
			LsaFreeMemory(translated_sid);
			print_message(stderr, NSSM_GUI_INVALID_USERNAME, username);
			return 6;
		}
	}

	LSA_TRUST_INFORMATION* trust = &translated_domains->Domains[translated_sid->DomainIndex];
	if (!trust || !IsValidSid(trust->Sid))
	{
		LsaFreeMemory(translated_domains);
		LsaFreeMemory(translated_sid);
		print_message(stderr, NSSM_GUI_INVALID_USERNAME, username);
		return 7;
	}

	/* GetSidSubAuthority*() return pointers! */
	uint8_t* n = GetSidSubAuthorityCount(trust->Sid);

	/* Convert translated SID to SID. */
	*sid = (SID*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, GetSidLengthRequired(*n + 1));
	if (!*sid)
	{
		LsaFreeMemory(translated_domains);
		LsaFreeMemory(translated_sid);
		print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"SID", L"username_sid");
		return 8;
	}

	uint32_t error;
	if (!InitializeSid(*sid, GetSidIdentifierAuthority(trust->Sid), *n + 1))
	{
		error = GetLastError();
		HeapFree(GetProcessHeap(), 0, *sid);
		LsaFreeMemory(translated_domains);
		LsaFreeMemory(translated_sid);
		print_message(stderr, NSSM_MESSAGE_INITIALIZESID_FAILED, username, error_string(error));
		return 9;
	}

	for (uint8_t i = 0; i <= *n; i++)
	{
		ULONG* sub = GetSidSubAuthority(*sid, i);
		if (i < *n)
			*sub = *GetSidSubAuthority(trust->Sid, i);
		else
			*sub = translated_sid->RelativeId;
	}

	ret = 0;
	if (translated_sid->Use == SidTypeWellKnownGroup && !well_known_sid(*sid))
	{
		print_message(stderr, NSSM_GUI_INVALID_USERNAME, username);
		ret = 10;
	}

	LsaFreeMemory(translated_domains);
	LsaFreeMemory(translated_sid);

	return ret;
}

int32_t username_sid(const wchar_t* username, SID** sid)
{
	return username_sid(username, sid, 0);
}

int32_t canonicalise_username(const wchar_t* username, wchar_t** canon)
{
	LSA_HANDLE policy;
	if (open_lsa_policy(&policy))
		return 1;

	SID* sid;
	if (username_sid(username, &sid, &policy))
		return 2;
	PSID sids = {sid};

	LSA_REFERENCED_DOMAIN_LIST* translated_domains;
	LSA_TRANSLATED_NAME* translated_name;
	NTSTATUS status = LsaLookupSids(policy, 1, &sids, &translated_domains, &translated_name);
	if (status != STATUS_SUCCESS)
	{
		LsaFreeMemory(translated_domains);
		LsaFreeMemory(translated_name);
		print_message(stderr, NSSM_MESSAGE_LSALOOKUPSIDS_FAILED, error_string(LsaNtStatusToWinError(status)));
		return 3;
	}

	LSA_TRUST_INFORMATION* trust = &translated_domains->Domains[translated_name->DomainIndex];
	LSA_UNICODE_STRING lsa_canon;
	lsa_canon.Length = translated_name->Name.Length + trust->Name.Length + sizeof(wchar_t);
	lsa_canon.MaximumLength = lsa_canon.Length + sizeof(wchar_t);
	lsa_canon.Buffer = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, lsa_canon.MaximumLength);
	if (!lsa_canon.Buffer)
	{
		LsaFreeMemory(translated_domains);
		LsaFreeMemory(translated_name);
		print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"lsa_canon", L"username_sid");
		return 9;
	}

	/* Buffer is wchar_t but Length is in bytes. */
	memmove((char*)lsa_canon.Buffer, trust->Name.Buffer, trust->Name.Length);
	memmove((char*)lsa_canon.Buffer + trust->Name.Length, L"\\", sizeof(wchar_t));
	memmove((char*)lsa_canon.Buffer + trust->Name.Length + sizeof(wchar_t), translated_name->Name.Buffer, translated_name->Name.Length);

	uint32_t canonlen;
	if (from_utf16(lsa_canon.Buffer, canon, &canonlen))
	{
		LsaFreeMemory(translated_domains);
		LsaFreeMemory(translated_name);
		print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"canon", L"username_sid");
		return 10;
	}
	HeapFree(GetProcessHeap(), 0, lsa_canon.Buffer);

	LsaFreeMemory(translated_domains);
	LsaFreeMemory(translated_name);

	return 0;
}

/* Do two usernames map to the same SID? */
int32_t username_equiv(const wchar_t* a, const wchar_t* b)
{
	SID *sid_a, *sid_b;
	if (username_sid(a, &sid_a))
		return 0;

	if (username_sid(b, &sid_b))
	{
		FreeSid(sid_a);
		return 0;
	}

	int32_t ret = 0;
	if (EqualSid(sid_a, sid_b))
		ret = 1;

	FreeSid(sid_a);
	FreeSid(sid_b);

	return ret;
}

/* Does the username represent the LocalSystem account? */
int32_t is_localsystem(const wchar_t* username)
{
	if (str_equiv(username, NSSM_LOCALSYSTEM_ACCOUNT))
		return 1;
	if (!imports.IsWellKnownSid)
		return 0;

	SID* sid;
	if (username_sid(username, &sid))
		return 0;

	int32_t ret = 0;
	if (imports.IsWellKnownSid(sid, WinLocalSystemSid))
		ret = 1;

	FreeSid(sid);

	return ret;
}

/* Build the virtual account name. */
wchar_t* virtual_account(const wchar_t* service_name)
{
	size_t len = ::wcslen(NSSM_VIRTUAL_SERVICE_ACCOUNT_DOMAIN) + ::wcslen(service_name) + 2;
	wchar_t* name = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, len * sizeof(wchar_t));
	if (!name)
	{
		print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, L"name", L"virtual_account");
		return 0;
	}

	::_snwprintf_s(name, len, _TRUNCATE, L"%s\\%s", NSSM_VIRTUAL_SERVICE_ACCOUNT_DOMAIN, service_name);
	return name;
}

/* Does the username represent a virtual account for the service? */
int32_t is_virtual_account(const wchar_t* service_name, const wchar_t* username)
{
	if (!imports.IsWellKnownSid)
		return 0;
	if (!service_name)
		return 0;
	if (!username)
		return 0;

	wchar_t* canon = virtual_account(service_name);
	int32_t ret = str_equiv(canon, username);
	HeapFree(GetProcessHeap(), 0, canon);
	return ret;
}

/*
  Get well-known alias for LocalSystem and friends.
  Returns a pointer to a static string.  DO NOT try to free it.
*/
const wchar_t* well_known_sid(SID* sid)
{
	if (!imports.IsWellKnownSid)
		return 0;
	if (imports.IsWellKnownSid(sid, WinLocalSystemSid))
		return NSSM_LOCALSYSTEM_ACCOUNT;
	if (imports.IsWellKnownSid(sid, WinLocalServiceSid))
		return NSSM_LOCALSERVICE_ACCOUNT;
	if (imports.IsWellKnownSid(sid, WinNetworkServiceSid))
		return NSSM_NETWORKSERVICE_ACCOUNT;
	return 0;
}

const wchar_t* well_known_username(const wchar_t* username)
{
	if (!username)
		return NSSM_LOCALSYSTEM_ACCOUNT;
	if (str_equiv(username, NSSM_LOCALSYSTEM_ACCOUNT))
		return NSSM_LOCALSYSTEM_ACCOUNT;
	SID* sid;
	if (username_sid(username, &sid))
		return 0;

	const wchar_t* well_known = well_known_sid(sid);
	FreeSid(sid);

	return well_known;
}

int32_t grant_logon_as_service(const wchar_t* username)
{
	if (!username)
		return 0;

	/* Open Policy object. */
	LSA_OBJECT_ATTRIBUTES attributes;
	ZeroMemory(&attributes, sizeof(attributes));

	LSA_HANDLE policy;
	NTSTATUS status;

	if (open_lsa_policy(&policy))
		return 1;

	/* Look up SID for the account. */
	SID* sid;
	if (username_sid(username, &sid, &policy))
	{
		LsaClose(policy);
		return 2;
	}

	/*
    Shouldn't happen because it should have been checked before callling this function.
  */
	if (well_known_sid(sid))
	{
		LsaClose(policy);
		return 3;
	}

	/* Check if the SID has the "Log on as a service" right. */
	LSA_UNICODE_STRING lsa_right;
	lsa_right.Buffer = NSSM_LOGON_AS_SERVICE_RIGHT;
	lsa_right.Length = (uint16_t)::wcslen(lsa_right.Buffer) * sizeof(wchar_t);
	lsa_right.MaximumLength = lsa_right.Length + sizeof(wchar_t);

	LSA_UNICODE_STRING* rights;
	uint32_t count = ~0;
	status = LsaEnumerateAccountRights(policy, sid, &rights, &count);
	if (status != STATUS_SUCCESS)
	{
		/*
      If the account has no rights set LsaEnumerateAccountRights() will return
      STATUS_OBJECT_NAME_NOT_FOUND and set count to 0.
    */
		uint32_t error = LsaNtStatusToWinError(status);
		if (error != ERROR_FILE_NOT_FOUND)
		{
			FreeSid(sid);
			LsaClose(policy);
			print_message(stderr, NSSM_MESSAGE_LSAENUMERATEACCOUNTRIGHTS_FAILED, username, error_string(error));
			return 4;
		}
	}

	for (uint32_t i = 0; i < count; i++)
	{
		if (rights[i].Length != lsa_right.Length)
			continue;
		if (_wcsnicmp(rights[i].Buffer, lsa_right.Buffer, lsa_right.MaximumLength))
			continue;
		/* The SID has the right. */
		FreeSid(sid);
		LsaFreeMemory(rights);
		LsaClose(policy);
		return 0;
	}
	LsaFreeMemory(rights);

	/* Add the right. */
	status = LsaAddAccountRights(policy, sid, &lsa_right, 1);
	FreeSid(sid);
	LsaClose(policy);
	if (status != STATUS_SUCCESS)
	{
		print_message(stderr, NSSM_MESSAGE_LSAADDACCOUNTRIGHTS_FAILED, error_string(LsaNtStatusToWinError(status)));
		return 5;
	}

	print_message(stdout, NSSM_MESSAGE_GRANTED_LOGON_AS_SERVICE, username);
	return 0;
}
