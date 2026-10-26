// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

namespace win32ui
{

// metrics from WM_GETFONT, or system font when unset
TEXTMETRICW WindowFontMetrics(HWND window);

} // namespace win32ui
