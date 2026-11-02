// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

namespace win32ui
{

std::vector<std::wstring> CommandLineArgs(const wchar_t* command_line = nullptr, DWORD* error = nullptr);
std::wstring QuoteArgument(std::wstring_view argument);
void AppendArgument(std::wstring* arguments, std::wstring_view argument);

DWORD LaunchProcess(HWND owner, const std::wstring& exe, const std::wstring& arguments);
DWORD LaunchElevated(HWND owner, const std::wstring& exe, const std::wstring& arguments);
// SYSTEM and TrustedInstaller need an elevated caller, the caller should end when restore_error is set
DWORD LaunchAsSystem(const std::wstring& exe, const std::wstring& arguments, DWORD* restore_error);
DWORD LaunchAsTrustedInstaller(const std::wstring& exe, const std::wstring& arguments, DWORD* restore_error);
DWORD LaunchAsShellUser(const std::wstring& exe, const std::wstring& arguments, DWORD* restore_error);

// with its id it names the process even after the id is reused
ULONGLONG ProcessCreationTime(DWORD process_id);
DWORD WaitForProcessExit(DWORD process_id, ULONGLONG creation_time, DWORD timeout_ms);

}
