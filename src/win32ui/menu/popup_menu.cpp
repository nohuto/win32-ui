// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/menu/popup_menu.h"

#include "win32ui/dialog/dialog_window.h"

#include <algorithm>
#include <string_view>

namespace win32ui
{

namespace
{

std::wstring Label(const wchar_t* text, const wchar_t* shortcut)
{
    return shortcut && *shortcut ? std::wstring(text) + L'\t' + shortcut : std::wstring(text);
}

bool UpdateItems(HMENU popup, const MenuHooks& hooks)
{
    bool any_enabled = false;
    const int count = GetMenuItemCount(popup);
    for (int index = 0; index < count; ++index)
    {
        const UINT id = GetMenuItemID(popup, index);
        // search by position, command lookup could stop at a duplicate in an earlier submenu
        const UINT position = static_cast<UINT>(index);
        if (id == static_cast<UINT>(-1))
        {
            const bool enabled = UpdateItems(GetSubMenu(popup, index), hooks);
            if (hooks.enabled)
            {
                EnableMenuItem(popup, position, MF_BYPOSITION | (enabled ? MF_ENABLED : MF_GRAYED));
            }
            any_enabled = any_enabled || enabled;
            continue;
        }
        if (id == 0)
        {
            continue;
        }
        const int command = static_cast<int>(id);
        const bool enabled = !hooks.enabled || hooks.enabled(hooks.context, command);
        if (hooks.enabled)
        {
            EnableMenuItem(popup, position, MF_BYPOSITION | (enabled ? MF_ENABLED : MF_GRAYED));
        }
        if (hooks.checked)
        {
            CheckMenuItem(popup, position, MF_BYPOSITION | (hooks.checked(hooks.context, command) ? MF_CHECKED : MF_UNCHECKED));
        }
        std::wstring text;
        if (hooks.text && hooks.text(hooks.context, command, &text))
        {
            MENUITEMINFOW item = {sizeof(item)};
            item.fMask = MIIM_STRING;
            item.dwTypeData = const_cast<wchar_t*>(text.c_str());
            SetMenuItemInfoW(popup, position, TRUE, &item);
        }
        any_enabled = any_enabled || enabled;
    }
    return any_enabled;
}

}

std::wstring MenuName(std::wstring text, size_t max_chars)
{
    constexpr std::wstring_view kEllipsis = L"...";
    if (max_chars && text.size() > max_chars)
    {
        const size_t dots = (std::min)(max_chars, kEllipsis.size());
        size_t keep = max_chars - dots;
        keep -= keep && IS_HIGH_SURROGATE(text[keep - 1]) ? 1 : 0;
        text.resize(keep);
        text += kEllipsis.substr(0, dots);
    }
    for (size_t amp = text.find(L'&'); amp != std::wstring::npos; amp = text.find(L'&', amp + 2))
    {
        text.insert(amp, 1, L'&');
    }
    return text;
}

bool IsMenuSeparator(HMENU menu, int position)
{
    MENUITEMINFOW item = {sizeof(item)};
    item.fMask = MIIM_FTYPE | MIIM_SUBMENU;
    return GetMenuItemInfoW(menu, static_cast<UINT>(position), TRUE, &item) && (item.fType & MFT_SEPARATOR) && !item.hSubMenu;
}

void RemoveMenuCommands(HMENU menu, std::span<const int> commands)
{
    for (int position = GetMenuItemCount(menu) - 1; position >= 0; --position)
    {
        HMENU sub = GetSubMenu(menu, position);
        const int sub_count = sub ? GetMenuItemCount(sub) : 0;
        if (sub)
        {
            RemoveMenuCommands(sub, commands);
        }
        const bool emptied = sub && sub_count > 0 && GetMenuItemCount(sub) == 0;
        const bool listed = !sub && std::ranges::find(commands, static_cast<int>(GetMenuItemID(menu, position))) != commands.end();
        if (emptied || listed)
        {
            DeleteMenu(menu, static_cast<UINT>(position), MF_BYPOSITION);
        }
    }
    for (int position = GetMenuItemCount(menu) - 1; position >= 0; --position)
    {
        if (IsMenuSeparator(menu, position) && (position == 0 || position == GetMenuItemCount(menu) - 1 || IsMenuSeparator(menu, position - 1)))
        {
            DeleteMenu(menu, static_cast<UINT>(position), MF_BYPOSITION);
        }
    }
}

int SubMenuWithCommand(HMENU menu, UINT command)
{
    const int count = menu ? GetMenuItemCount(menu) : 0;
    for (int position = 0; position < count; ++position)
    {
        HMENU sub = GetSubMenu(menu, position);
        const int sub_count = sub ? GetMenuItemCount(sub) : 0;
        for (int item = 0; item < sub_count; ++item)
        {
            // GetMenuItemID gives -1 for an item opening a submenu
            if (!GetSubMenu(sub, item) && GetMenuItemID(sub, item) == command)
            {
                return position;
            }
        }
    }
    return -1;
}

bool MenuContains(HMENU root, HMENU menu)
{
    const int count = root && menu ? GetMenuItemCount(root) : 0;
    for (int position = 0; position < count; ++position)
    {
        HMENU child = GetSubMenu(root, position);
        if (child && (child == menu || MenuContains(child, menu)))
        {
            return true;
        }
    }
    return false;
}

void AppendMenuItem(HMENU menu, UINT flags, UINT_PTR id, const wchar_t* text, const wchar_t* shortcut)
{
    AppendMenuW(menu, flags, id, Label(text, shortcut).c_str());
}

void AppendRadioItem(HMENU menu, UINT_PTR id, const wchar_t* text, const wchar_t* shortcut)
{
    const std::wstring label = Label(text, shortcut);
    MENUITEMINFOW item = {sizeof(item)};
    item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING;
    item.fType = MFT_STRING | MFT_RADIOCHECK;
    item.wID = static_cast<UINT>(id);
    item.dwTypeData = const_cast<wchar_t*>(label.c_str());
    InsertMenuItemW(menu, static_cast<UINT>(GetMenuItemCount(menu)), TRUE, &item);
}

int TrackContextMenu(HMENU menu, POINT screen, HWND owner)
{
    int command = 0;
    {
        const ModalScope modal;
        command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, owner, nullptr);
    }
    DestroyMenu(menu);
    return command;
}

int TrackDropdownMenu(HMENU menu, const RECT& button, HWND owner)
{
    const ModalScope modal;
    return TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, button.left, button.bottom, 0, owner, nullptr);
}

void UpdateMenuPopup(HMENU popup, const MenuHooks& hooks)
{
    UpdateItems(popup, hooks);
}

void ReplaceMenuRange(HMENU popup, UINT first, UINT last, UINT before, std::span<const std::wstring> labels)
{
    int lowest = -1;
    for (int position = GetMenuItemCount(popup) - 1; position >= 0; --position)
    {
        const UINT id = GetMenuItemID(popup, position);
        if (id != static_cast<UINT>(-1) && id >= first && id <= last)
        {
            DeleteMenu(popup, static_cast<UINT>(position), MF_BYPOSITION);
            lowest = position;
        }
    }
    if (lowest > 0 && IsMenuSeparator(popup, lowest - 1))
    {
        DeleteMenu(popup, static_cast<UINT>(lowest - 1), MF_BYPOSITION);
    }

    const size_t count = std::min(labels.size(), static_cast<size_t>(last - first) + 1);
    if (std::none_of(labels.begin(), labels.begin() + static_cast<ptrdiff_t>(count), [](const std::wstring& label) { return !label.empty(); }))
    {
        return;
    }
    int position = GetMenuItemCount(popup);
    // separators have the id 0
    for (int index = 0; before != 0 && index < GetMenuItemCount(popup); ++index)
    {
        if (GetMenuItemID(popup, index) == before)
        {
            position = index > 0 && IsMenuSeparator(popup, index - 1) ? index - 1 : index;
            break;
        }
    }
    if (position > 0)
    {
        InsertMenuW(popup, static_cast<UINT>(position++), MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    }
    for (size_t index = 0; index < count; ++index)
    {
        if (!labels[index].empty())
        {
            InsertMenuW(popup, static_cast<UINT>(position++), MF_BYPOSITION | MF_STRING, first + index, labels[index].c_str());
        }
    }
}

}
