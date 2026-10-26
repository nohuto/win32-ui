// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/menu/shortcuts.h"

#include <vector>

namespace win32ui
{

const wchar_t* ShortcutText(std::span<const Shortcut> shortcuts, int command)
{
    for (const Shortcut& shortcut : shortcuts)
    {
        if (shortcut.command == command && shortcut.text)
        {
            return shortcut.text;
        }
    }
    return nullptr;
}

ShortcutTable::~ShortcutTable()
{
    if (accelerators_)
    {
        DestroyAcceleratorTable(accelerators_);
    }
}

void ShortcutTable::Create(std::span<const Shortcut> shortcuts)
{
    std::vector<ACCEL> table;
    table.reserve(shortcuts.size());
    for (const Shortcut& shortcut : shortcuts)
    {
        table.push_back({static_cast<BYTE>(FVIRTKEY | shortcut.modifiers), shortcut.key, static_cast<WORD>(shortcut.command)});
    }
    if (accelerators_)
    {
        DestroyAcceleratorTable(accelerators_);
    }
    accelerators_ = table.empty() ? nullptr : CreateAcceleratorTableW(table.data(), static_cast<int>(table.size()));
}

bool TranslateShortcuts(HWND main, const ShortcutTable& table, MSG* message, bool (*keep)(const MSG& message))
{
    if (!table.accelerators() || GetAncestor(message->hwnd, GA_ROOT) != main || (keep && keep(*message)))
    {
        return false;
    }
    return TranslateAcceleratorW(main, table.accelerators(), message) != FALSE;
}

} // namespace win32ui
