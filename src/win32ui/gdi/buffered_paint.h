// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

namespace win32ui
{

// buffers WM_PRINTCLIENT to handle WM_PAINT without flicker
void PaintBuffered(HWND window);

} // namespace win32ui
