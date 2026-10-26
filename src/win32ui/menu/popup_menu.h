// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <span>
#include <string>

namespace win32ui
{

// escapes ampersands so menu labels display them without creating access keys
std::wstring MenuName(std::wstring text);
// adds right aligned shortcut text after a tab
void AppendMenuItem(HMENU menu, UINT flags, UINT_PTR id, const wchar_t* text, const wchar_t* shortcut = nullptr);
// exclusive choice item, checked state uses a bullet
void AppendRadioItem(HMENU menu, UINT_PTR id, const wchar_t* text, const wchar_t* shortcut = nullptr);
// shows popup at screen and destroys it, returns command or 0
// close requests wait for the menu loop, like modal dialogs
int TrackContextMenu(HMENU menu, POINT screen, HWND owner);
// shows below button in screen coordinates, keeps the menu, returns command or 0
int TrackDropdownMenu(HMENU menu, const RECT& button, HWND owner);

// app command state, null hooks leave the corresponding item state unchanged
struct MenuHooks
{
    void* context = nullptr;
    bool (*enabled)(void* context, int command) = nullptr;
    bool (*checked)(void* context, int command) = nullptr;
    // new item text, false keeps existing text
    bool (*text)(void* context, int command, std::wstring* text) = nullptr;
};

// updates popup and submenu items from WM_INITMENUPOPUP
// disables submenus with no enabled commands
void UpdateMenuPopup(HMENU popup, const MenuHooks& hooks);

// replaces ids first through last with labels, empty labels reserve unused ids
// inserts a group and separator above the separator preceding before, or appends if absent
// no labels leave no group
void ReplaceMenuRange(HMENU popup, UINT first, UINT last, UINT before, std::span<const std::wstring> labels);

} // namespace win32ui
