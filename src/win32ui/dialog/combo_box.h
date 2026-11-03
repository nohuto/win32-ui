// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <span>
#include <string>
#include <vector>

namespace win32ui
{

void SetComboItems(HWND combo, const std::vector<std::wstring>& items);
// item data is the label index
void FilterComboItems(HWND combo, std::span<const std::wstring> labels, int kept, bool filtering);
void SetComboIndex(HWND combo, int index);
bool SelectComboText(HWND combo, const wchar_t* text);
int ComboFitWidth(HWND combo);
void FitDroppedWidth(HWND combo);

}
