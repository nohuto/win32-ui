// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

namespace win32ui
{

// returned pointers must stay valid until exit
using TextHook = const wchar_t* (*)(const wchar_t* english);

// set once before windows or workers call Text
void SetTextHook(TextHook hook) noexcept;
bool HasTextHook() noexcept;
const wchar_t* Text(const wchar_t* english) noexcept;
// marks a text for extraction (keyword TextNoop) where a table holds it and Text runs later
constexpr const wchar_t* TextNoop(const wchar_t* english) noexcept
{
    return english;
}
void TranslateWindowTexts(HWND window);

}
