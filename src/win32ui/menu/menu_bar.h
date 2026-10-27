// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/handle_owner.h"

#include <windows.h>

#include <memory>
#include <vector>

namespace win32ui
{

// owner drawn top level menu items in theme colors, native popups follow dark mode
class MenuBar
{
  public:
    MenuBar();
    ~MenuBar();
    MenuBar(const MenuBar&) = delete;
    MenuBar& operator=(const MenuBar&) = delete;

    // replaces and destroys the previous menu, window owns bar
    void Attach(HWND window, HMENU bar);
    void ApplyTheme();
    // handles WM_MEASUREITEM and WM_DRAWITEM
    bool Measure(MEASUREITEMSTRUCT* info) const;
    bool Draw(const DRAWITEMSTRUCT* info) const;
    // call after DefWindowProc handles WM_NCPAINT or WM_NCACTIVATE to cover the light line under the bar
    void PaintSeparator() const;

  private:
    struct Item;
    const Item* Find(ULONG_PTR data) const;

    HWND window_ = nullptr;
    HMENU menu_ = nullptr;
    UniqueFont font_;
    std::vector<std::unique_ptr<Item>> items_;
};

} // namespace win32ui
