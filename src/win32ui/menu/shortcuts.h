// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <span>

namespace win32ui
{

struct Shortcut
{
    int command;
    BYTE modifiers; // FCONTROL, FSHIFT, FALT
    WORD key;
    const wchar_t* text;
};

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

bool TranslateShortcuts(HWND main, const ShortcutTable& table, MSG* message, bool (*keep)(const MSG& message));

}
