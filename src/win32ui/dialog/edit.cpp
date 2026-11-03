// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/dialog/edit.h"

#include "win32ui/gdi/fonts.h"
#include "win32ui/menu/popup_menu.h"
#include "win32ui/text/text.h"
#include "win32ui/window.h"
#include "win32ui/window_metrics.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cwchar>
#include <iterator>
#include <string>

namespace win32ui
{

namespace
{

constexpr UINT_PTR kSingleLineSubclassId = 1;
constexpr UINT_PTR kSelectAllSubclassId = 2;
constexpr UINT_PTR kSelectOnFocusSubclassId = 3;
constexpr int kMenuOffsetDip = 8;

LRESULT CALLBACK SelectAllProc(HWND edit, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR)
{
    if (message == WM_KEYDOWN && wparam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000))
    {
        SendMessageW(edit, EM_SETSEL, 0, -1);
        return 0;
    }
    if (message == WM_CHAR && wparam == 1)
    {
        return 0;
    }
    if (message == WM_NCDESTROY)
    {
        RemoveWindowSubclass(edit, SelectAllProc, kSelectAllSubclassId);
    }
    return DefSubclassProc(edit, message, wparam, lparam);
}

bool IsEditControl(HWND window)
{
    wchar_t name[32] = {};
    GetClassNameW(window, name, static_cast<int>(std::size(name)));
    return _wcsicmp(name, L"Edit") == 0 || _wcsnicmp(name, L"RichEdit", 8) == 0;
}

void AddSelectAll(HWND edit)
{
    if (IsEditControl(edit))
    {
        EnsureSubclass(edit, SelectAllProc, kSelectAllSubclassId);
    }
}

LRESULT CALLBACK SingleLineProc(HWND edit, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR)
{
    if (message == WM_CHAR && (wparam == L'\r' || wparam == L'\n'))
    {
        return 0;
    }
    // EM_REPLACESEL would also change a read-only edit
    if (message == WM_PASTE && !(GetWindowLongPtrW(edit, GWL_STYLE) & ES_READONLY))
    {
        std::wstring text;
        if (OpenClipboard(edit))
        {
            HANDLE handle = GetClipboardData(CF_UNICODETEXT);
            if (const auto* data = handle ? static_cast<const wchar_t*>(GlobalLock(handle)) : nullptr)
            {
                // the allocation need not hold a NUL
                text.assign(data, wcsnlen(data, GlobalSize(handle) / sizeof(wchar_t)));
                GlobalUnlock(handle);
            }
            CloseClipboard();
        }
        if (text.find_first_of(L"\r\n") == std::wstring::npos)
        {
            return DefSubclassProc(edit, message, wparam, lparam);
        }
        for (wchar_t& character : text)
        {
            if (character == L'\r' || character == L'\n')
            {
                character = L' ';
            }
        }
        SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text.c_str()));
        return 0;
    }
    if (message == WM_NCDESTROY)
    {
        RemoveWindowSubclass(edit, SingleLineProc, kSingleLineSubclassId);
    }
    return DefSubclassProc(edit, message, wparam, lparam);
}

LRESULT CALLBACK SelectOnFocusProc(HWND edit, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR)
{
    // click that focuses the edit places the caret after WM_SETFOCUS, so it selects again
    if (message == WM_SETFOCUS || (message == WM_LBUTTONDOWN && GetFocus() != edit))
    {
        const LRESULT result = DefSubclassProc(edit, message, wparam, lparam);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        SendMessageW(edit, EM_SCROLLCARET, 0, 0);
        return result;
    }
    if (message == WM_NCDESTROY)
    {
        RemoveWindowSubclass(edit, SelectOnFocusProc, kSelectOnFocusSubclassId);
    }
    return DefSubclassProc(edit, message, wparam, lparam);
}

}

void KeepSingleLine(HWND edit)
{
    if (edit)
    {
        SetWindowSubclass(edit, SingleLineProc, kSingleLineSubclassId, 0);
    }
}

void AllowLineBreaks(HWND edit)
{
    if (edit)
    {
        RemoveWindowSubclass(edit, SingleLineProc, kSingleLineSubclassId);
    }
}

void AddSelectAllShortcut(HWND window)
{
    if (!window)
    {
        return;
    }
    AddSelectAll(window);
    EnumChildWindows(
        window,
        [](HWND child, LPARAM) -> BOOL {
            AddSelectAll(child);
            return TRUE;
        },
        0
    );
}

void SelectAllOnFocus(HWND edit)
{
    if (edit)
    {
        EnsureSubclass(edit, SelectOnFocusProc, kSelectOnFocusSubclassId);
    }
}

void SetEditPadding(HWND edit, int horizontal_dip, int min_vertical_dip)
{
    if (!edit)
    {
        return;
    }
    const UINT dpi = WindowDpi(edit);
    const int horizontal = Scale(horizontal_dip, dpi);
    SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(horizontal, horizontal));
    RECT rect = {};
    GetClientRect(edit, &rect);
    rect.left += horizontal;
    rect.right -= horizontal;
    int pad = Scale(min_vertical_dip, dpi);
    const TEXTMETRICW metrics = WindowFontMetrics(edit);
    if (metrics.tmHeight > 0)
    {
        const int available = rect.bottom - rect.top;
        pad = std::max(pad, (available - static_cast<int>(metrics.tmHeight + metrics.tmExternalLeading)) / 2);
        const int line = std::min(static_cast<int>(metrics.tmHeight + metrics.tmExternalLeading), std::max(1, available - pad * 2));
        rect.top += pad;
        rect.bottom = rect.top + line;
    }
    else
    {
        rect.top += pad;
        rect.bottom -= pad;
    }
    SendMessageW(edit, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&rect));
}

UINT ShowEditContextMenu(HWND edit, LPARAM screen_point, const EditMenuCommand* extra)
{
    HMENU menu = edit ? CreatePopupMenu() : nullptr;
    if (!menu)
    {
        return 0;
    }
    POINT point = {GET_X_LPARAM(screen_point), GET_Y_LPARAM(screen_point)};
    if (point.x == -1 && point.y == -1)
    {
        RECT rect = {};
        GetWindowRect(edit, &rect);
        point = {rect.left + Scale(kMenuOffsetDip, WindowDpi(edit)), rect.top + (rect.bottom - rect.top) / 2};
    }
    DWORD start = 0;
    DWORD end = 0;
    SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    const bool selection = end > start;
    const bool writable = !(GetWindowLongPtrW(edit, GWL_STYLE) & ES_READONLY);
    const bool clipboard_text = IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;
    const auto append = [menu](bool enabled, UINT id, const wchar_t* text, const wchar_t* shortcut) {
        AppendMenuItem(menu, MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED), id, text, shortcut);
    };
    append(writable && SendMessageW(edit, EM_CANUNDO, 0, 0), WM_UNDO, Text(L"&Undo"), L"Ctrl+Z");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    append(selection && writable, WM_CUT, Text(L"Cu&t"), L"Ctrl+X");
    append(selection, WM_COPY, Text(L"&Copy"), L"Ctrl+C");
    append(clipboard_text && writable, WM_PASTE, Text(L"&Paste"), L"Ctrl+V");
    if (extra)
    {
        append(extra->enabled, extra->id, extra->text, extra->shortcut);
    }
    append(selection && writable, WM_CLEAR, Text(L"&Delete"), L"Del");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    append(GetWindowTextLengthW(edit) > 0, EM_SETSEL, Text(L"Select &All"), L"Ctrl+A");
    const UINT command = static_cast<UINT>(TrackContextMenu(menu, point, GetAncestor(edit, GA_ROOT)));
    if (extra && command == extra->id)
    {
        return command;
    }
    if (command == EM_SETSEL)
    {
        SendMessageW(edit, EM_SETSEL, 0, -1);
    }
    else if (command == WM_UNDO || command == WM_CUT || command == WM_COPY || command == WM_PASTE || command == WM_CLEAR)
    {
        SendMessageW(edit, command, 0, 0);
    }
    return 0;
}

}
