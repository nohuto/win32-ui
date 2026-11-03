// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace win32ui
{

struct CheckListSpec
{
    std::wstring_view title;
    std::span<const std::wstring> labels;
    bool (*accept)(void* context, HWND dialog, const std::vector<size_t>& checked) = nullptr;
    void* context = nullptr;
};

DWORD PickChecks(HWND owner, const CheckListSpec& spec, std::vector<size_t>* checked);

}
