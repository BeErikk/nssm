/*******************************************************************************
 nssm_pch.h  : precompiled header and include file for system include files

 This code is free for use by anyone under the terms of
 Creative Commons Attribution-NonCommercial 4.0 International Public License
 See https://creativecommons.org

 2025-05-23 Jerker Bäck, created

********************************************************************************
 RcsID = $Id$ */

#pragma once

// clang-format off

#if defined(__GNUC__) || defined(__clang__)
#define MSDISABLE_WARNING(x)
#define MSDISABLE_WARNING_PUSH(x)
#define MSDISABLE_WARNING_POP
#define MSSUPPRESS_WARNING(x)
#define MSADD_LIBRARY(x)
#define GCCPRAGMA(x)                _Pragma(#x)
#define GCCDISABLE_WARNING(x)       GCCPRAGMA(GCC diagnostic ignored #x)
#define GCCDISABLE_WARNING_PUSH     GCCPRAGMA(GCC diagnostic push)
#define GCCDISABLE_WARNING_POP      GCCPRAGMA(GCC diagnostic pop)
#elif defined(_MSC_VER)
#define MSDISABLE_WARNING(x)		__pragma(warning(disable:x))
#define MSDISABLE_WARNING_PUSH(x)	__pragma(warning(push));__pragma(warning(disable:x))
#define MSDISABLE_WARNING_POP		__pragma(warning(pop))
#define MSSUPPRESS_WARNING(x)		__pragma(warning(suppress:x))
#define MSADD_LIBRARY(x)			__pragma(comment(lib,x))
#define GCCDISABLE_WARNING(x)
#define GCCDISABLE_WARNING_PUSH
#define GCCDISABLE_WARNING_POP
#else
#define MSDISABLE_WARNING(x)
#define MSDISABLE_WARNING_PUSH(x)
#define MSDISABLE_WARNING_POP
#define MSSUPPRESS_WARNING(x)
#define MSADD_LIBRARY(x)
#define GCCDISABLE_WARNING(x)
#define GCCDISABLE_WARNING_PUSH
#define GCCDISABLE_WARNING_POP
#endif

// disable clang warnings
GCCDISABLE_WARNING(-Wc++98-compat-pedantic)
GCCDISABLE_WARNING(-Wunused-command-line-argument)
GCCDISABLE_WARNING_PUSH
GCCDISABLE_WARNING(-Wreserved-id-macro)
GCCDISABLE_WARNING(-Wmicrosoft-include)

#define STRICT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRTDBG_MAP_ALLOC

#define  _WIN32_WINNT   0x0601	// minimum Windows 7
#include <winsdkver.h>
#include <sdkddkver.h>
#ifndef WINAPI_FAMILY
#define WINAPI_FAMILY WINAPI_FAMILY_DESKTOP_APP
#endif

#define CONSIDERED_UNUSED 0

// C, C++ library
MSDISABLE_WARNING_PUSH(4514 5262 5264)
#include <cstddef>
#include <cstdint>
#include <sys/types.h>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
//#include <cmath>
//#include <cerrno>
//#include <algorithm>
//#include <any>
#include <array>
//#include <charconv>
#include <chrono>
//#include <complex>
//#include <concepts>
#include <exception>
//#include <filesystem>
//#include <fstream>
//#include <functional>
//#include <iomanip>
//#include <iostream>
//#include <iterator>
//#include <limits>
//#include <list>
//#include <locale>
//#include <map>
//#include <memory>
//#include <numeric>
//#include <optional>
//#include <ranges>
//#include <set>
//#include <span>
//#include <sstream>
//#include <stdexcept>
//#include <streambuf>
#include <string>
#include <string_view>
//#include <tuple>
#include <type_traits>
#include <bit>
//#include <utility>
//#include <variant>
#include <vector>
//#include <sys/stat.h>
//#include <crtdbg.h>
#include <fcntl.h>
#include <io.h>
//#include <tchar.h>
MSDISABLE_WARNING_POP
GCCDISABLE_WARNING_POP

MSDISABLE_WARNING_PUSH(4820 5039)
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <psapi.h>
#include <ntsecapi.h>
#include <tlhelp32.h>
#include <sddl.h>
#include <commctrl.h>
#include <prsht.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
MSDISABLE_WARNING_POP

//MSDISABLE_WARNING_PUSH(4061 4365 4514 4625 4626 4820 5026 5027 6326)
MSDISABLE_WARNING_PUSH(4623 4626 4820 4866 5027 5045 /*5246*/)
//#include <CLI/CLI.hpp>
//#include <argparse/argparse.hpp>
MSDISABLE_WARNING_POP

// google test
//MSDISABLE_WARNING_PUSH(4514 4625 4626 4668 4820 5026 5027 5264 6326)
//#include <gtest/gtest.h>
//#include <gmock/gmock.h>
//MSADD_LIBRARY("gtest.lib")
//MSADD_LIBRARY("gtest_main.lib")
//MSADD_LIBRARY("gmock.lib")
////MSADD_LIBRARY("gmock_main.lib")
//MSDISABLE_WARNING_POP

// enum bit mask operations
#ifndef ENABLE_ENUM_BITMASKS
#define ENABLE_ENUM_BITMASKS(e) _BITMASK_OPS(, e) // <type_traits>
#endif

MSDISABLE_WARNING(4191 4365 4820 5039)

// 4514 4571 4625 4710 4820

// clang-format on


