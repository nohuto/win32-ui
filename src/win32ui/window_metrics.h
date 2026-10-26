// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

namespace win32ui
{

UINT DpiForWindow(HWND window);

// synthetic layout DPI, 0 disables it, changes layout and fonts without changing process DPI context
void SetDpiOverride(UINT dpi);
UINT DpiOverride();

// window DPI, overridden by synthetic layout DPI when set
UINT WindowDpi(HWND hwnd);
inline int Scale(int dip, UINT dpi)
{
    return MulDiv(dip, static_cast<int>(dpi), 96);
}
inline int Unscale(int pixels, UINT dpi)
{
    return dpi ? MulDiv(pixels, 96, static_cast<int>(dpi)) : pixels;
}

bool AdjustWindowRectForDpi(RECT* rect, DWORD style, DWORD ex_style, UINT dpi);

int SystemMetricForDpi(int index, UINT dpi);

void ClampToWorkArea(RECT* rect);

} // namespace win32ui
