// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

namespace win32ui
{

DWORD ShellOpen(HWND owner, const wchar_t* target, const wchar_t* file_class = nullptr);
DWORD RevealInExplorer(const wchar_t* path);

}
