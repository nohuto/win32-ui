// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

namespace win32ui
{

void PaintBuffered(HWND window);
LRESULT HandleFillPaint(HWND window, UINT message, WPARAM wparam, HBRUSH brush);

}
