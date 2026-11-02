// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>

namespace win32ui
{

using RegGetValueFunction = LSTATUS(WINAPI*)(HKEY, LPCWSTR, LPCWSTR, DWORD, LPDWORD, PVOID, LPDWORD);

DWORD ReadRegistryStringWith(RegGetValueFunction get_value, HKEY root, const wchar_t* subkey, const wchar_t* name, std::wstring* value, DWORD flags);

}
