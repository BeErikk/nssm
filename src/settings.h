/*******************************************************************************
 settings.h - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#pragma once

#ifndef SETTINGS_H
#define SETTINGS_H

// clang-format off

/* Are additional arguments needed? */
enum class additionalarg : uint8_t
{
	none			= 0,
	getting			= 1 << 0,	// ADDITIONAL_GETTING
	setting			= 1 << 1,	// ADDITIONAL_SETTING
	resetting		= 1 << 2,	// ADDITIONAL_RESETTING
	crlf			= 1 << 3,	// ADDITIONAL_CRLF
	mandatory		= getting | setting | resetting	// ADDITIONAL_MANDATORY
};
ENABLE_ENUM_BITMASKS(additionalarg);

enum class dependency : uint8_t
{
	none			= 0,
	services		= 1 << 0,	// DEPENDENCY_SERVICES
	groups			= 1 << 1,	// DEPENDENCY_GROUPS
	all				= services | groups	// DEPENDENCY_ALL
};
ENABLE_ENUM_BITMASKS(dependency);


namespace nativeliterals
{
constexpr std::wstring_view dependongroup			{L"DependOnGroup"};		// NSSM_NATIVE_DEPENDONGROUP
constexpr std::wstring_view dependonservice			{L"DependOnService"};	// NSSM_NATIVE_DEPENDONSERVICE
constexpr std::wstring_view description				{L"Description"};		// NSSM_NATIVE_DESCRIPTION
constexpr std::wstring_view displayname				{L"DisplayName"};		// NSSM_NATIVE_DISPLAYNAME
constexpr std::wstring_view environment				{L"Environment"};		// NSSM_NATIVE_ENVIRONMENT
constexpr std::wstring_view imagepath				{L"ImagePath"};			// NSSM_NATIVE_IMAGEPATH
constexpr std::wstring_view name					{L"Name"};				// NSSM_NATIVE_NAME
constexpr std::wstring_view objectname				{L"ObjectName"};		// NSSM_NATIVE_OBJECTNAME
constexpr std::wstring_view start					{L"Start"};				// NSSM_NATIVE_STARTUP
constexpr std::wstring_view type					{L"Type"};				// NSSM_NATIVE_TYPE
}
// clang-format on

union value_t
{
	uint32_t numeric;
	wchar_t* string;
};

typedef int32_t (*setting_function_t)(const wchar_t*, void*, const wchar_t*, void*, value_t*, const wchar_t*);

struct settings_t
{
	const wchar_t* name;
	uint32_t type;
	void* default_value;
	bool native;
	additionalarg additional;
	setting_function_t set;
	setting_function_t get;
	setting_function_t dump;
};

int32_t set_setting(const wchar_t*, HKEY, settings_t*, value_t*, const wchar_t*);
int32_t set_setting(const wchar_t*, SC_HANDLE, settings_t*, value_t*, const wchar_t*);
int32_t get_setting(const wchar_t*, HKEY, settings_t*, value_t*, const wchar_t*);
int32_t get_setting(const wchar_t*, SC_HANDLE, settings_t*, value_t*, const wchar_t*);
int32_t dump_setting(const wchar_t*, HKEY, SC_HANDLE, settings_t*);

#endif
