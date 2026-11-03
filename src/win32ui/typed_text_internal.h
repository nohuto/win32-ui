// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <cwctype>
#include <string>

namespace win32ui
{

struct TypedText
{
    std::wstring text;
    DWORD at = 0;

    bool Alive() const noexcept
    {
        return !text.empty() && GetTickCount() - at <= 1000;
    }

    // false for non search characters, spaces only extend existing text
    bool Feed(wchar_t character)
    {
        if (character != L'\b' && (!std::iswprint(character) || character == L'\t'))
        {
            return false;
        }
        if (!Alive())
        {
            text.clear();
        }
        if (character == L' ' && text.empty())
        {
            return true;
        }
        at = GetTickCount();
        if (character != L'\b')
        {
            text.push_back(character);
        }
        else if (!text.empty())
        {
            text.pop_back();
        }
        return true;
    }
};

}
