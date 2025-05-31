/*******************************************************************************
 hook.h - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#pragma once

#ifndef HOOK_H
#define HOOK_H

// clang-format off

enum class hookconst : uint16_t
{
	namelength		= SERVICE_NAME_LENGTH * 2,	/* Hook name will be "<service> (<event>/<action>)" */
	version			= 1,
};
enum class nssmhook : uint8_t
{
	none			= UINT8_MAX,
	success			= 0,			/* Hook ran successfully. */
	notfound		= 1,			/* No hook configured. */
	abort			= 99,			/* Hook requested abort. */
	error			= 100,			/* Internal error launching hook. */
	notrun			= 101,			/* Hook was not run. */
	timeout			= 102,			/* Hook timed out. */
	failed			= 111			/* Hook returned non-zero. */

};

namespace hook
{
constexpr std::wstring_view eventstart			{ L"Start"};						// NSSM_HOOK_EVENT_START
constexpr std::wstring_view eventstop			{ L"Stop" };						// NSSM_HOOK_EVENT_STOP
constexpr std::wstring_view eventexit			{ L"Exit" };						// NSSM_HOOK_EVENT_EXIT
constexpr std::wstring_view eventpower			{ L"Power" };						// NSSM_HOOK_EVENT_POWER
constexpr std::wstring_view eventrotate			{ L"Rotate" };						// NSSM_HOOK_EVENT_ROTATE

constexpr std::wstring_view actionpre			{ L"Pre" };							// NSSM_HOOK_ACTION_PRE
constexpr std::wstring_view actionpost			{ L"Post" };						// NSSM_HOOK_ACTION_POST
constexpr std::wstring_view actionchange		{ L"Change" };						// NSSM_HOOK_ACTION_CHANGE
constexpr std::wstring_view actionresume		{ L"Resume" };						// NSSM_HOOK_ACTION_RESUME
} // namespace hook

/* Version 1. */
namespace hookenv
{
constexpr std::wstring_view hookversion			{ L"NSSM_HOOK_VERSION" };			// NSSM_HOOK_ENV_VERSION
constexpr std::wstring_view imagepath			{ L"NSSM_EXE" };					// NSSM_HOOK_ENV_IMAGE_PATH
constexpr std::wstring_view config				{ L"NSSM_CONFIGURATION" };			// NSSM_HOOK_ENV_NSSM_CONFIGURATION
constexpr std::wstring_view version				{ L"NSSM_VERSION" };				// NSSM_HOOK_ENV_NSSM_VERSION
constexpr std::wstring_view builddate			{ L"NSSM_BUILD_DATE" };				// NSSM_HOOK_ENV_BUILD_DATE
constexpr std::wstring_view pid					{ L"NSSM_PID" };					// NSSM_HOOK_ENV_PID
constexpr std::wstring_view deadline			{ L"NSSM_DEADLINE" };				// NSSM_HOOK_ENV_DEADLINE
constexpr std::wstring_view servicename			{ L"NSSM_SERVICE_NAME" };			// NSSM_HOOK_ENV_SERVICE_NAME
constexpr std::wstring_view displayname			{ L"NSSM_SERVICE_DISPLAYNAME" };	// NSSM_HOOK_ENV_SERVICE_DISPLAYNAME
constexpr std::wstring_view commandline			{ L"NSSM_COMMAND_LINE" };			// NSSM_HOOK_ENV_COMMAND_LINE
constexpr std::wstring_view apppid				{ L"NSSM_APPLICATION_PID" };		// NSSM_HOOK_ENV_APPLICATION_PID
constexpr std::wstring_view event				{ L"NSSM_EVENT" };					// NSSM_HOOK_ENV_EVENT
constexpr std::wstring_view action				{ L"NSSM_ACTION" };					// NSSM_HOOK_ENV_ACTION
constexpr std::wstring_view trigger				{ L"NSSM_TRIGGER" };				// NSSM_HOOK_ENV_TRIGGER
constexpr std::wstring_view lastcontrol			{ L"NSSM_LAST_CONTROL" };			// NSSM_HOOK_ENV_LAST_CONTROL
constexpr std::wstring_view startrequestedcount	{ L"NSSM_START_REQUESTED_COUNT" };	// NSSM_HOOK_ENV_START_REQUESTED_COUNT
constexpr std::wstring_view startcount			{ L"NSSM_START_COUNT" };			// NSSM_HOOK_ENV_START_COUNT
constexpr std::wstring_view throttlecount		{ L"NSSM_THROTTLE_COUNT" };			// NSSM_HOOK_ENV_THROTTLE_COUNT
constexpr std::wstring_view exitcount			{ L"NSSM_EXIT_COUNT" };				// NSSM_HOOK_ENV_EXIT_COUNT
constexpr std::wstring_view exitcode			{ L"NSSM_EXITCODE" };				// NSSM_HOOK_ENV_EXITCODE
constexpr std::wstring_view runtime				{ L"NSSM_RUNTIME" };				// NSSM_HOOK_ENV_RUNTIME
constexpr std::wstring_view appruntime			{ L"NSSM_APPLICATION_RUNTIME" };	// NSSM_HOOK_ENV_APPLICATION_RUNTIME
} // namespace hookenv

struct hook_thread_data_t
{
	wchar_t name[std::to_underlying(hookconst::namelength)] {};
	HANDLE thread_handle {};
};

struct hook_thread_t
{
	hook_thread_data_t* data {};
	int32_t num_threads {};
};

// clang-format on

bool valid_hook_name(const wchar_t*, const wchar_t*, bool);
void await_hook_threads(hook_thread_t*, SERVICE_STATUS_HANDLE, SERVICE_STATUS*, uint32_t);
nssmhook nssm_hook(hook_thread_t*, nssm_service_t*, wchar_t*, wchar_t*, uint32_t*, uint32_t, bool);
nssmhook nssm_hook(hook_thread_t*, nssm_service_t*, wchar_t*, wchar_t*, uint32_t*, uint32_t);
nssmhook nssm_hook(hook_thread_t*, nssm_service_t*, wchar_t*, wchar_t*, uint32_t*);

#endif
