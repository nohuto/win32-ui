// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <span>
#include <string>

namespace win32ui
{

std::wstring MenuName(std::wstring text, size_t max_chars = 0);
bool IsMenuSeparator(HMENU menu, int position);
void RemoveMenuCommands(HMENU menu, std::span<const int> commands);
int SubMenuWithCommand(HMENU menu, UINT command);
bool MenuContains(HMENU root, HMENU menu);

void AppendMenuItem(HMENU menu, UINT flags, UINT_PTR id, const wchar_t* text, const wchar_t* shortcut = nullptr);
void AppendRadioItem(HMENU menu, UINT_PTR id, const wchar_t* text, const wchar_t* shortcut = nullptr);
int TrackContextMenu(HMENU menu, POINT screen, HWND owner); // menu is destroyed afterwards
int TrackDropdownMenu(HMENU menu, const RECT& button, HWND owner);

struct MenuHooks
{
    void* context = nullptr;
    bool (*enabled)(void* context, int command) = nullptr;
    bool (*checked)(void* context, int command) = nullptr;
    bool (*text)(void* context, int command, std::wstring* text) = nullptr;
};

// call from WM_INITMENUPOPUP
void UpdateMenuPopup(HMENU popup, const MenuHooks& hooks);

void ReplaceMenuRange(HMENU popup, UINT first, UINT last, UINT before, std::span<const std::wstring> labels);

}
