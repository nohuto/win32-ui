// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/dialog/single_line_edit.h"

#include <commctrl.h>

#include <cwchar>
#include <iterator>
#include <string>

namespace win32ui
{

namespace
{

constexpr UINT_PTR kSingleLineSubclassId = 1;
constexpr UINT_PTR kSelectAllSubclassId = 2;

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
    if (IsEditControl(edit) && !GetWindowSubclass(edit, SelectAllProc, kSelectAllSubclassId, nullptr))
    {
        SetWindowSubclass(edit, SelectAllProc, kSelectAllSubclassId, 0);
    }
}

LRESULT CALLBACK SingleLineProc(HWND edit, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR)
{
    if (message == WM_CHAR && (wparam == L'\r' || wparam == L'\n'))
    {
        return 0;
    }
    if (message == WM_PASTE)
    {
        std::wstring text;
        if (OpenClipboard(edit))
        {
            HANDLE handle = GetClipboardData(CF_UNICODETEXT);
            if (const auto* data = handle ? static_cast<const wchar_t*>(GlobalLock(handle)) : nullptr)
            {
                text = data;
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

} // namespace

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

} // namespace win32ui
