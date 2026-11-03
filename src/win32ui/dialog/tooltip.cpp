// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/dialog/tooltip.h"

#include "win32ui/dialog/dialog_layout_internal.h"
#include "win32ui/window_metrics.h"

#include <commctrl.h>

#include <algorithm>

namespace win32ui
{

namespace
{

constexpr int kTipMaxWidthDip = 600;
constexpr int kTipCursorGapDip = 18;

// tip is a popup without the child DPI messages its control gets
LRESULT CALLBACK ToolDpiProc(HWND control, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data)
{
    if (message == 0x02E2) // WM_DPICHANGED_BEFOREPARENT, not in the targeted SDK
    {
        SendMessageW(reinterpret_cast<HWND>(data), TTM_SETMAXTIPWIDTH, 0, Scale(kTipMaxWidthDip, WindowDpi(control)));
    }
    else if (message == WM_NCDESTROY)
    {
        RemoveWindowSubclass(control, ToolDpiProc, id);
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

// a tip destroyed first ends the DPI link of its control
LRESULT CALLBACK TipDestroyProc(HWND tip, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data)
{
    if (message == WM_NCDESTROY)
    {
        RemoveWindowSubclass(reinterpret_cast<HWND>(data), ToolDpiProc, reinterpret_cast<UINT_PTR>(tip));
        RemoveWindowSubclass(tip, TipDestroyProc, id);
    }
    return DefSubclassProc(tip, message, wparam, lparam);
}

}

HWND AddTooltip(HWND owner, HWND control, const wchar_t* text)
{
    if (!owner || !control || !text)
    {
        return nullptr;
    }
    const HWND tip = CreateTooltip(owner);
    if (!tip)
    {
        return nullptr;
    }
    TOOLINFOW info = {};
    info.cbSize = sizeof(info);
    info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    info.hwnd = owner;
    info.uId = reinterpret_cast<UINT_PTR>(control);
    info.lpszText = const_cast<wchar_t*>(text);
    if (!SendMessageW(tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info)))
    {
        DestroyWindow(tip);
        return nullptr;
    }
    SendMessageW(tip, TTM_SETMAXTIPWIDTH, 0, Scale(kTipMaxWidthDip, WindowDpi(owner)));
    if (SetWindowSubclass(control, ToolDpiProc, reinterpret_cast<UINT_PTR>(tip), reinterpret_cast<DWORD_PTR>(tip)))
    {
        SetWindowSubclass(tip, TipDestroyProc, 1, reinterpret_cast<DWORD_PTR>(control));
    }
    return tip;
}

bool PlaceTipNearCursor(HWND tip, UINT dpi)
{
    RECT rect = {};
    POINT cursor = {};
    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    if (!GetWindowRect(tip, &rect) || !GetCursorPos(&cursor) || !GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor))
    {
        return false;
    }
    const int gap = Scale(kTipCursorGapDip, dpi);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    int x = cursor.x + gap;
    int y = cursor.y + gap;
    if (x + width > monitor.rcWork.right)
    {
        x = std::max<int>(monitor.rcWork.left, cursor.x - gap - width);
    }
    if (y + height > monitor.rcWork.bottom)
    {
        y = std::max<int>(monitor.rcWork.top, cursor.y - gap - height);
    }
    return SetWindowPos(tip, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE) != FALSE;
}

}
