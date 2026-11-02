// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

namespace win32ui
{

bool CaptureNormalRect(HWND window, RECT* normal, bool* maximized);
// call before it is shown, capture and restore need the same WS_EX_TOOLWINDOW state
void RestoreNormalRect(HWND window, const RECT* normal, UINT saved_dpi, SIZE default_size_dip);

}
