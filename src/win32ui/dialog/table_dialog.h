// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/list_view/sort.h"

#include <windows.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace win32ui
{

struct TextTable
{
    std::wstring title;
    std::vector<std::wstring> columns;
    TextRows rows;
};

struct TablesSpec
{
    std::wstring_view title;
    std::wstring_view identifier;
    std::span<const TextTable> tables;
    std::wstring_view action_label;
    void (*action)(void* context, HWND dialog) = nullptr;
    int check_table = -1;
    std::vector<bool> checked;
    bool (*on_check)(void* context, HWND dialog, size_t row, bool checked) = nullptr;
    void* context = nullptr;
};

void ShowTables(HWND owner, const TablesSpec& spec);

}
