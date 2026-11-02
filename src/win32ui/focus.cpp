// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/focus.h"

#include <commctrl.h>

#include <cwchar>

namespace win32ui
{

namespace
{

bool IsComboClass(const wchar_t* name)
{
    return _wcsicmp(name, WC_COMBOBOXW) == 0 || _wcsicmp(name, WC_COMBOBOXEXW) == 0;
}

// disabled parent disables its children without changing their own state, top is checked too
bool EnabledUpTo(HWND window, HWND top)
{
    for (HWND current = window; current; current = GetAncestor(current, GA_PARENT))
    {
        if (!IsWindowEnabled(current))
        {
            return false;
        }
        if (current == top)
        {
            break;
        }
    }
    return true;
}

bool Usable(HWND window, HWND pane)
{
    RECT area = {};
    return pane && IsWindowVisible(pane) && EnabledUpTo(pane, window) && GetClientRect(pane, &area) && !IsRectEmpty(&area);
}

}

bool IsTextInput(HWND window) noexcept
{
    // longer names are cut, a cut name equals none of these, "RichEdit" covers RICHEDIT, RichEdit20A/W and RICHEDIT50W
    wchar_t name[32] = {};
    if (!window || !GetClassNameW(window, name, static_cast<int>(std::size(name))))
    {
        return false;
    }
    if (_wcsicmp(name, WC_EDITW) == 0 || _wcsnicmp(name, L"RichEdit", 8) == 0 || IsComboClass(name))
    {
        return true;
    }
    const HWND parent = GetParent(window);
    if (parent && GetClassNameW(parent, name, static_cast<int>(std::size(name))) && IsComboClass(name))
    {
        return true;
    }
    // edits under other class names (superclasses) select text like an edit, only asked of windows of this thread, a
    // send to another thread waits for it
    return GetWindowThreadProcessId(window, nullptr) == GetCurrentThreadId() && (SendMessageW(window, WM_GETDLGCODE, 0, 0) & DLGC_HASSETSEL) != 0;
}

bool TextInputOwnsKey(const MSG& message) noexcept
{
    if ((message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN) || !IsTextInput(GetFocus()))
    {
        return false;
    }
    switch (message.wParam)
    {
    case VK_DELETE:
    case VK_BACK:
    case VK_INSERT:
    case VK_F2:
        return true;
    case VK_UP:
    case VK_DOWN:
        return (GetKeyState(VK_MENU) & 0x8000) == 0;
    case 'A':
    case 'C':
    case 'V':
    case 'X':
    case 'Y':
    case 'Z':
        return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    default:
        return false;
    }
}

HWND CyclePane(HWND window, std::span<const HWND> panes, bool forward) noexcept
{
    const HWND focus = GetFocus();
    const size_t count = panes.size();
    size_t current = count;
    for (size_t i = 0; i < count && current == count; ++i)
    {
        if (focus && Usable(window, panes[i]) && (panes[i] == focus || IsChild(panes[i], focus)))
        {
            current = i;
        }
    }
    // focused pane that is the only usable one comes back to itself
    for (size_t step = 1; step <= count; ++step)
    {
        size_t index = 0;
        if (current == count)
        {
            index = forward ? step - 1 : count - step;
        }
        else
        {
            index = forward ? (current + step) % count : (current + count - step) % count;
        }
        if (Usable(window, panes[index]))
        {
            // keyboard moves focus, panes show it
            SendMessageW(window, WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS), 0);
            return panes[index];
        }
    }
    return nullptr;
}

HWND ActivationFocus(HWND window, WPARAM wparam, HWND* last) noexcept
{
    if (!window || !last)
    {
        return nullptr;
    }
    if (LOWORD(wparam) == WA_INACTIVE)
    {
        const HWND focus = GetFocus();
        if (focus && IsChild(window, focus))
        {
            *last = focus;
        }
        return nullptr;
    }
    // like DefWindowProc, a minimized window gets no focus
    const HWND remembered = *last;
    return !HIWORD(wparam) && remembered && IsWindow(remembered) && IsChild(window, remembered) && IsWindowVisible(remembered) && EnabledUpTo(remembered, window) ? remembered : nullptr;
}

}
