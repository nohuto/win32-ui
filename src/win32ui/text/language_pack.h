// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

namespace win32ui
{

struct LanguagePack
{
    std::wstring code;
    std::wstring name;
};

// translations stay valid until exit
DWORD LoadLanguage(const wchar_t* folder, std::wstring_view code);
std::vector<LanguagePack> InstalledLanguages(const wchar_t* folder);
const wchar_t* Translate(const wchar_t* english) noexcept;

}
