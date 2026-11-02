// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>

namespace win32ui
{

DWORD ReadRegistryString(HKEY root, const wchar_t* subkey, const wchar_t* name, std::wstring* value, DWORD flags = 0);
DWORD ReadRegistryDword(HKEY root, const wchar_t* subkey, const wchar_t* name, DWORD* value, DWORD flags = 0) noexcept;
DWORD WriteRegistryString(HKEY root, const wchar_t* subkey, const wchar_t* name, const wchar_t* value) noexcept;

}
