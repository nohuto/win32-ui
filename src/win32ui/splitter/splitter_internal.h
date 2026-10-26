// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

namespace win32ui
{

// after a theme or DPI change
void RefreshSplitter(HWND splitter);

} // namespace win32ui
