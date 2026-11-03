// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/window_placement.h"

#include "win32ui/window_metrics.h"

#include <algorithm>

namespace win32ui
{

namespace
{

// workspace coordinates start at the work area of the monitor of the rect, like GetWindowPlacement
POINT WorkspaceOffset(const RECT& rect)
{
    MONITORINFO info = {sizeof(info)};
    const HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTOPRIMARY);
    if (!monitor || !GetMonitorInfoW(monitor, &info))
    {
        return {};
    }
    return {info.rcWork.left - info.rcMonitor.left, info.rcWork.top - info.rcMonitor.top};
}

}

bool CaptureNormalRect(HWND window, RECT* normal, bool* maximized)
{
    WINDOWPLACEMENT placement = {sizeof(placement)};
    if (!window || !normal || !GetWindowPlacement(window, &placement) || IsRectEmpty(&placement.rcNormalPosition))
    {
        return false;
    }
    *normal = placement.rcNormalPosition;
    if (maximized)
    {
        *maximized = placement.showCmd == SW_SHOWMAXIMIZED || IsZoomed(window);
    }
    return true;
}

void RestoreNormalRect(HWND window, const RECT* normal, UINT saved_dpi, SIZE default_size_dip)
{
    if (!window)
    {
        return;
    }
    const UINT dpi = WindowDpi(window);
    RECT rect = {};
    if (normal && !IsRectEmpty(normal))
    {
        // tool windows have their placement in screen coordinates
        const bool tool = (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0;
        const POINT offset = tool ? POINT{} : WorkspaceOffset(*normal);
        int width = normal->right - normal->left;
        int height = normal->bottom - normal->top;
        if (saved_dpi && saved_dpi != dpi)
        {
            width = MulDiv(width, static_cast<int>(dpi), static_cast<int>(saved_dpi));
            height = MulDiv(height, static_cast<int>(dpi), static_cast<int>(saved_dpi));
        }
        rect = {normal->left + offset.x, normal->top + offset.y, normal->left + offset.x + width, normal->top + offset.y + height};
    }
    else
    {
        MONITORINFO info = {sizeof(info)};
        if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &info))
        {
            return;
        }
        const RECT& work = info.rcWork;
        const LONG width = std::min<LONG>(Scale(default_size_dip.cx, dpi), work.right - work.left);
        const LONG height = std::min<LONG>(Scale(default_size_dip.cy, dpi), work.bottom - work.top);
        rect.left = work.left + (work.right - work.left - width) / 2;
        rect.top = work.top + (work.bottom - work.top - height) / 2;
        rect.right = rect.left + width;
        rect.bottom = rect.top + height;
    }
    ClampToWorkArea(&rect);
    SetWindowPos(window, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

}
