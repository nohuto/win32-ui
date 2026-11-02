// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>

namespace win32ui
{

// WinHTTP codes need a loaded winhttp.dll
std::wstring FormatWin32Error(DWORD code);
// module must already be loaded
std::wstring FormatModuleError(DWORD code, const wchar_t* module);
DWORD Win32FromNtStatus(LONG status) noexcept;
DWORD Win32FromHResult(HRESULT hr) noexcept;

}
