/*******************************************************************************
 registry.h - 

 SPDX-License-Identifier: CC0 1.0 Universal Public Domain
 Original author Iain Patterson released nssm under Public Domain
 https://creativecommons.org/publicdomain/zero/1.0/

 NSSM source code - the Non-Sucking Service Manager

 2025-05-31 and onwards modified Jerker Bäck

*******************************************************************************/

#pragma once

#ifndef REGISTRY_H
#define REGISTRY_H

// clang-format off

enum class registry_const : uint8_t
{
    stdiolength = 29,	// NSSM_STDIO_LENGTH
};

namespace regliterals
{
constexpr std::wstring_view regpath                     {L"SYSTEM\\CurrentControlSet\\Services\\%s"};               // NSSM_REGISTRY
constexpr std::wstring_view regparameters               {L"Parameters"};                                            // NSSM_REG_PARAMETERS
constexpr std::wstring_view reggroupspath               {L"SYSTEM\\CurrentControlSet\\Control\\ServiceGroupOrder"}; // NSSM_REGISTRY_GROUPS
constexpr std::wstring_view reggroups                   {L"List"};                                                  // NSSM_REG_GROUPS
constexpr std::wstring_view regexe                      {L"Application"};                                           // NSSM_REG_EXE
constexpr std::wstring_view regflags                    {L"AppParameters"};                                         // NSSM_REG_FLAGS
constexpr std::wstring_view regdir                      {L"AppDirectory"};                                          // NSSM_REG_DIR
constexpr std::wstring_view regenv                      {L"AppEnvironment"};                                        // NSSM_REG_ENV
constexpr std::wstring_view regenvextra                 {L"AppEnvironmentExtra"};                                   // NSSM_REG_ENV_EXTRA
constexpr std::wstring_view regexit                     {L"AppExit"};                                               // NSSM_REG_EXIT
constexpr std::wstring_view regrestartdelay             {L"AppRestartDelay"};                                       // NSSM_REG_RESTART_DELAY
constexpr std::wstring_view regthrottle                 {L"AppThrottle"};                                           // NSSM_REG_THROTTLE
constexpr std::wstring_view regstopmethodskip           {L"AppStopMethodSkip"};                                     // NSSM_REG_STOP_METHOD_SKIP
constexpr std::wstring_view regkillconsolegraceperiod   {L"AppStopMethodConsole"};                                  // NSSM_REG_KILL_CONSOLE_GRACE_PERIOD
constexpr std::wstring_view regkillwindowgraceperiod    {L"AppStopMethodWindow"};                                   // NSSM_REG_KILL_WINDOW_GRACE_PERIOD
constexpr std::wstring_view regkillthreadsgraceperiod   {L"AppStopMethodThreads"};                                  // NSSM_REG_KILL_THREADS_GRACE_PERIOD
constexpr std::wstring_view regkillprocesstree          {L"AppKillProcessTree"};                                    // NSSM_REG_KILL_PROCESS_TREE
constexpr std::wstring_view regstdin                    {L"AppStdin"};                                              // NSSM_REG_STDIN
constexpr std::wstring_view regstdout                   {L"AppStdout"};                                             // NSSM_REG_STDOUT
constexpr std::wstring_view regstderr                   {L"AppStderr"};                                             // NSSM_REG_STDERR
constexpr std::wstring_view regsharemode                {L"ShareMode"};                                             // NSSM_REG_STDIO_SHARING
constexpr std::wstring_view regdisposition              {L"CreationDisposition"};                                   // NSSM_REG_STDIO_DISPOSITION
constexpr std::wstring_view regstdioflags               {L"FlagsAndAttributes"};                                    // NSSM_REG_STDIO_FLAGS
constexpr std::wstring_view regcopytruncate             {L"CopyAndTruncate"};                                       // NSSM_REG_STDIO_COPY_AND_TRUNCATE
constexpr std::wstring_view regredirecthook             {L"AppRedirectHook"};                                       // NSSM_REG_HOOK_SHARE_OUTPUT_HANDLES
constexpr std::wstring_view regrotate                   {L"AppRotateFiles"};                                        // NSSM_REG_ROTATE
constexpr std::wstring_view regrotateonline             {L"AppRotateOnline"};                                       // NSSM_REG_ROTATE_ONLINE
constexpr std::wstring_view regrotateseconds            {L"AppRotateSeconds"};                                      // NSSM_REG_ROTATE_SECONDS
constexpr std::wstring_view regrotatebyteslow           {L"AppRotateBytes"};                                        // NSSM_REG_ROTATE_BYTES_LOW
constexpr std::wstring_view regrotatebyteshigh          {L"AppRotateBytesHigh"};                                    // NSSM_REG_ROTATE_BYTES_HIGH
constexpr std::wstring_view regrotatedelay              {L"AppRotateDelay"};                                        // NSSM_REG_ROTATE_DELAY
constexpr std::wstring_view regtimestamplog             {L"AppTimestampLog"};                                       // NSSM_REG_TIMESTAMP_LOG
constexpr std::wstring_view regpriority                 {L"AppPriority"};                                           // NSSM_REG_PRIORITY
constexpr std::wstring_view regaffinity                 {L"AppAffinity"};                                           // NSSM_REG_AFFINITY
constexpr std::wstring_view regnoconsole                {L"AppNoConsole"};                                          // NSSM_REG_NO_CONSOLE
constexpr std::wstring_view reghook                     {L"AppEvents"};                                             // NSSM_REG_HOOK
}
// clang-format on

HKEY open_service_registry(const wchar_t*, REGSAM sam, bool);
int32_t open_registry(const wchar_t*, const wchar_t*, REGSAM sam, HKEY*, bool);
HKEY open_registry(const wchar_t*, const wchar_t*, REGSAM sam, bool);
HKEY open_registry(const wchar_t*, const wchar_t*, REGSAM sam);
HKEY open_registry(const wchar_t*, REGSAM sam);
int32_t enumerate_registry_values(HKEY, uint32_t*, wchar_t*, uint32_t);
int32_t create_messages();
int32_t create_parameters(nssm_service_t*, bool);
int32_t create_exit_action(wchar_t*, const wchar_t*, bool);
int32_t get_environment(wchar_t*, HKEY, wchar_t*, wchar_t**, uint32_t*);
int32_t get_string(HKEY, wchar_t*, wchar_t*, uint32_t, bool, bool, bool);
int32_t get_string(HKEY, wchar_t*, wchar_t*, uint32_t, bool);
int32_t expand_parameter(HKEY, wchar_t*, wchar_t*, uint32_t, bool, bool);
int32_t expand_parameter(HKEY, wchar_t*, wchar_t*, uint32_t, bool);
int32_t set_string(HKEY, wchar_t*, wchar_t*, bool);
int32_t set_string(HKEY, const wchar_t*, wchar_t*);
int32_t set_expand_string(HKEY, const wchar_t*, wchar_t*);
int32_t set_number(HKEY, wchar_t*, uint32_t);
int32_t get_number(HKEY, wchar_t*, uint32_t*, bool);
int32_t get_number(HKEY, wchar_t*, uint32_t*);
int32_t format_double_null(wchar_t*, uint32_t, wchar_t**, uint32_t*);
int32_t unformat_double_null(wchar_t*, uint32_t, wchar_t**, uint32_t*);
int32_t copy_double_null(wchar_t*, uint32_t, wchar_t**);
int32_t append_to_double_null(wchar_t*, uint32_t, wchar_t**, uint32_t*, wchar_t*, size_t, bool);
int32_t remove_from_double_null(wchar_t*, uint32_t, wchar_t**, uint32_t*, wchar_t*, size_t, bool);
void override_milliseconds(wchar_t*, HKEY, wchar_t*, uint32_t*, uint32_t, uint32_t);
int32_t get_io_parameters(nssm_service_t*, HKEY);
int32_t get_parameters(nssm_service_t*, STARTUPINFOW*);
int32_t get_exit_action(const wchar_t*, uint32_t*, wchar_t*, bool*);
int32_t set_hook(const wchar_t*, const wchar_t*, const wchar_t*, wchar_t*);
int32_t get_hook(const wchar_t*, const wchar_t*, const wchar_t*, wchar_t*, uint32_t);

#endif
