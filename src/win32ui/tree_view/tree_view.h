// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <commctrl.h>

#include <string>
#include <string_view>

namespace win32ui
{

struct TreeViewHooks
{
    void (*load_children)(void* context, HWND tree, HTREEITEM item) = nullptr;
    bool (*is_group)(void* context, HWND tree, HTREEITEM item) = nullptr;
    bool (*expand_all)(void* context, HWND tree, HTREEITEM item) = nullptr;
    void* context = nullptr;
};

// call on the thread of the tree, hooks (may be null) outlive it
bool AttachTreeView(HWND tree, const TreeViewHooks* hooks);
// typed text still extends, so Backspace belongs to the tree
bool TreeTypeSearchActive(HWND tree);

std::wstring TreeItemText(HWND tree, HTREEITEM item);
bool TreeItemData(HWND tree, HTREEITEM item, LPARAM* data);
HTREEITEM FindTreeChild(HWND tree, HTREEITEM parent, std::wstring_view text);
HTREEITEM ContextMenuTreeItem(HWND tree, LPARAM screen_point, POINT* screen);
void SetTreeChecksBelow(HWND tree, HTREEITEM item, bool checked);

}
