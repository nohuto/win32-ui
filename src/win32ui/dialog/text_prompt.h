// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace win32ui
{

struct TextPrompt
{
    std::wstring_view title;
    std::wstring_view label;
    bool multiline = false;
    bool (*browse)(void* context, HWND owner, std::wstring* line) = nullptr;
    void* context = nullptr;
};

DWORD PromptText(HWND owner, const TextPrompt& prompt, std::wstring* text);

}
