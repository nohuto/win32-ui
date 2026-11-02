// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace win32ui
{

// polls up to wait_ms since a starting instance holds its lock before its window exists
HWND FindInstanceWindow(const wchar_t* property, DWORD wait_ms, DWORD skip_id = 0, ULONGLONG skip_created = 0);

DWORD SendInstanceText(HWND window, ULONG_PTR id, std::wstring_view text, DWORD timeout_ms);

// filters stray and foreign messages, it is no authentication
bool ReadInstanceText(WPARAM wparam, LPARAM lparam, size_t max_chars, ULONG_PTR* id, std::wstring* text);

}
