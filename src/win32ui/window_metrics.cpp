// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

#include "win32ui/window_metrics.h"

#include "win32ui/system_api.h"

#include <algorithm>

namespace win32ui
{

namespace
{
UINT g_dpi_override = 0;
}

void SetDpiOverride(UINT dpi)
{
    g_dpi_override = dpi;
}

UINT DpiOverride()
{
    return g_dpi_override;
}

UINT WindowDpi(HWND hwnd)
{
    return g_dpi_override ? g_dpi_override : DpiForWindow(hwnd);
}

UINT DpiForWindow(HWND window)
{
    static const auto get_window_dpi = ImportProc<UINT(WINAPI*)(HWND)>(L"user32.dll", "GetDpiForWindow");
    static const auto get_system_dpi = ImportProc<UINT(WINAPI*)()>(L"user32.dll", "GetDpiForSystem");
    UINT dpi = window && get_window_dpi ? get_window_dpi(window) : 0;
    if (dpi == 0 && get_system_dpi)
    {
        dpi = get_system_dpi();
    }
    if (dpi != 0)
    {
        return dpi;
    }
    HDC dc = GetDC(window);
    const int caps = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc)
    {
        ReleaseDC(window, dc);
    }
    return caps > 0 ? static_cast<UINT>(caps) : 96;
}

bool AdjustWindowRectForDpi(RECT* rect, DWORD style, DWORD ex_style, UINT dpi)
{
    if (!rect)
    {
        return false;
    }
    static const auto adjust_for_dpi = ImportProc<BOOL(WINAPI*)(RECT*, DWORD, BOOL, DWORD, UINT)>(L"user32.dll", "AdjustWindowRectExForDpi");
    return (adjust_for_dpi && adjust_for_dpi(rect, style, FALSE, ex_style, dpi)) || AdjustWindowRectEx(rect, style, FALSE, ex_style) != FALSE;
}

int SystemMetricForDpi(int index, UINT dpi)
{
    static const auto metric_for_dpi = ImportProc<int(WINAPI*)(int, UINT)>(L"user32.dll", "GetSystemMetricsForDpi");
    return metric_for_dpi ? metric_for_dpi(index, dpi) : GetSystemMetrics(index);
}

void ClampToWorkArea(RECT* rect)
{
    if (!rect || rect->right <= rect->left || rect->bottom <= rect->top)
    {
        return;
    }
    RECT work = {};
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    const HMONITOR monitor = MonitorFromRect(rect, MONITOR_DEFAULTTONEAREST);
    if (monitor && GetMonitorInfoW(monitor, &info))
    {
        work = info.rcWork;
    }
    else if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0))
    {
        return;
    }
    if (work.right <= work.left || work.bottom <= work.top)
    {
        return;
    }
    const LONG width = std::min(rect->right - rect->left, work.right - work.left);
    const LONG height = std::min(rect->bottom - rect->top, work.bottom - work.top);
    const LONG left = std::clamp(rect->left, work.left, work.right - width);
    const LONG top = std::clamp(rect->top, work.top, work.bottom - height);
    *rect = {left, top, left + width, top + height};
}

} // namespace win32ui
