// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <span>

namespace win32ui
{

// shared list for accelerators and shortcut text in menus and toolbar tips
struct Shortcut
{
    int command;
    BYTE modifiers; // FCONTROL, FSHIFT, FALT
    WORD key;
    // null for a key that shows nowhere
    const wchar_t* text;
};

// text of the first named shortcut for a command, or null
const wchar_t* ShortcutText(std::span<const Shortcut> shortcuts, int command);

class ShortcutTable
{
  public:
    ShortcutTable() = default;
    ~ShortcutTable();
    ShortcutTable(const ShortcutTable&) = delete;
    ShortcutTable& operator=(const ShortcutTable&) = delete;

    void Create(std::span<const Shortcut> shortcuts);
    HACCEL accelerators() const noexcept
    {
        return accelerators_;
    }

  private:
    HACCEL accelerators_ = nullptr;
};

// translates key messages for main or its children, true if translated
// keys claimed by keep remain with the focused control
bool TranslateShortcuts(HWND main, const ShortcutTable& table, MSG* message, bool (*keep)(const MSG& message));

} // namespace win32ui
