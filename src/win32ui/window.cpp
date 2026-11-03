// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/window.h"

#include <algorithm>
#include <climits>
#include <cwchar>

namespace win32ui
{

namespace
{

// one replacement character, a full copy means the text grew meanwhile and is read again with more room
std::wstring ReadText(HWND window, int length)
{
    constexpr int kMaxLength = INT_MAX - 2;
    std::wstring text;
    for (;;)
    {
        length = std::clamp(length, 0, kMaxLength);
        text.resize(static_cast<size_t>(length) + 1);
        const int copied = GetWindowTextW(window, text.data(), length + 2);
        if (copied <= length || length == kMaxLength)
        {
            text.resize(static_cast<size_t>(std::clamp(copied, 0, length + 1)));
            return text;
        }
        length = std::max(GetWindowTextLengthW(window), length > kMaxLength / 2 ? kMaxLength : std::max(length * 2, 16));
    }
}

}

std::wstring WindowText(HWND window)
{
    return window ? ReadText(window, GetWindowTextLengthW(window)) : std::wstring();
}

std::wstring DialogItemText(HWND dialog, int id)
{
    return dialog ? WindowText(GetDlgItem(dialog, id)) : std::wstring();
}

bool SetTextIfChanged(HWND window, const wchar_t* text)
{
    if (!window || !text)
    {
        return false;
    }
    // length is one message, the text is read only when the lengths match
    const int length = GetWindowTextLengthW(window);
    if (length >= 0 && static_cast<size_t>(length) == std::wcslen(text) && ReadText(window, length) == text)
    {
        return false;
    }
    return SetWindowTextW(window, text) != FALSE;
}

bool SetEnabled(HWND window, bool enabled) noexcept
{
    if (!window || (IsWindowEnabled(window) != FALSE) == enabled)
    {
        return false;
    }
    EnableWindow(window, enabled);
    return true;
}

bool SetVisible(HWND window, bool visible) noexcept
{
    if (!window || ((GetWindowLongPtrW(window, GWL_STYLE) & WS_VISIBLE) != 0) == visible)
    {
        return false;
    }
    ShowWindow(window, visible ? SW_SHOWNA : SW_HIDE);
    return true;
}

void BringToFront(HWND window) noexcept
{
    if (IsIconic(window))
    {
        ShowWindow(window, SW_RESTORE);
    }
    SetForegroundWindow(window);
}

bool ClassIs(HWND window, const wchar_t* name) noexcept
{
    // class names have up to 256 characters
    wchar_t buffer[257];
    return window && GetClassNameW(window, buffer, static_cast<int>(std::size(buffer))) > 0 && _wcsicmp(buffer, name) == 0;
}

bool ChildRect(HWND parent, HWND child, RECT* rect) noexcept
{
    if (!parent || !child || !GetWindowRect(child, rect))
    {
        *rect = {};
        return false;
    }
    // 0 is also the result of a mapping without offset
    SetLastError(ERROR_SUCCESS);
    if (!MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(rect), 2) && GetLastError() != ERROR_SUCCESS)
    {
        *rect = {};
        return false;
    }
    return true;
}

bool EnsureSubclass(HWND window, SUBCLASSPROC proc, UINT_PTR id) noexcept
{
    return window && (GetWindowSubclass(window, proc, id, nullptr) || SetWindowSubclass(window, proc, id, 0));
}

DWORD RegisterWindowClass(HINSTANCE instance, const wchar_t* name, WNDPROC proc, int icon, UINT style, int window_extra) noexcept
{
    WNDCLASSEXW window_class = {sizeof(window_class)};
    window_class.style = style;
    window_class.cbWndExtra = window_extra;
    window_class.lpfnWndProc = proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (icon)
    {
        // shared icons (standard sizes) are never destroyed by hand, so no path leaks one
        window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(icon));
        window_class.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(icon), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR | LR_SHARED));
    }
    window_class.lpszClassName = name;
    if (RegisterClassExW(&window_class))
    {
        return ERROR_SUCCESS;
    }
    const DWORD error = GetLastError();
    return error == ERROR_CLASS_ALREADY_EXISTS ? ERROR_SUCCESS : error;
}

RedrawScope::RedrawScope(HWND window, UINT redraw_flags) noexcept
    : window_(window && (GetWindowLongPtrW(window, GWL_STYLE) & WS_VISIBLE) ? window : nullptr), flags_(redraw_flags)
{
    if (window_)
    {
        SendMessageW(window_, WM_SETREDRAW, FALSE, 0);
    }
}

RedrawScope::~RedrawScope()
{
    if (window_)
    {
        SendMessageW(window_, WM_SETREDRAW, TRUE, 0);
        if (flags_)
        {
            RedrawWindow(window_, nullptr, nullptr, flags_);
        }
    }
}

}
