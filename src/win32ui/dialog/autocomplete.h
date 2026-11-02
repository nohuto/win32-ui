// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace win32ui
{

// runs on the thread of the edit
using AutoCompleteSuggest = std::function<std::vector<std::wstring>(const std::wstring& text)>;

// call on the thread of the edit with COM initialized
bool AttachAutoComplete(HWND edit, AutoCompleteSuggest suggest);
void SetAutoCompleteEnabled(bool enabled) noexcept;
// only the popups of the calling thread
void ApplyAutoCompleteTheme();
std::vector<std::wstring> SuggestComboPaths(HWND combo, std::wstring_view text);

}
