// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string_view>

namespace win32ui
{

void SetAccessibleName(HWND window, std::wstring_view name);
// call before CoUninitialize
void ReleaseAccessibility();

} // namespace win32ui
