// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

#pragma once

namespace win32ui
{

inline int HexDigitValue(wchar_t character)
{
    if (character >= L'0' && character <= L'9')
    {
        return character - L'0';
    }
    if (character >= L'a' && character <= L'f')
    {
        return character - L'a' + 10;
    }
    if (character >= L'A' && character <= L'F')
    {
        return character - L'A' + 10;
    }
    return -1;
}

} // namespace win32ui
