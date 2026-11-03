// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <commctrl.h>

#include <string>

namespace win32ui
{

std::wstring WindowText(HWND window);
std::wstring DialogItemText(HWND dialog, int id);
bool SetTextIfChanged(HWND window, const wchar_t* text);
bool SetEnabled(HWND window, bool enabled) noexcept;
// SetVisible checks the style bit (a child of a hidden parent counts as shown)
bool SetVisible(HWND window, bool visible) noexcept;
void BringToFront(HWND window) noexcept;
bool ClassIs(HWND window, const wchar_t* name) noexcept;
bool ChildRect(HWND parent, HWND child, RECT* rect) noexcept;
bool EnsureSubclass(HWND window, SUBCLASSPROC proc, UINT_PTR id) noexcept;
// a class registered before counts as success
DWORD RegisterWindowClass(HINSTANCE instance, const wchar_t* name, WNDPROC proc, int icon, UINT style = 0, int window_extra = 0) noexcept;

// hidden windows are left alone, WM_SETREDRAW TRUE would show them, dont nest scopes on one window
class RedrawScope
{
  public:
    RedrawScope(HWND window, UINT redraw_flags) noexcept;
    ~RedrawScope();
    RedrawScope(const RedrawScope&) = delete;
    RedrawScope& operator=(const RedrawScope&) = delete;

  private:
    HWND window_;
    UINT flags_;
};

}
