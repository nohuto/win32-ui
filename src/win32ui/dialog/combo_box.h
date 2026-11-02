// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace win32ui
{

void SetComboItems(HWND combo, const std::vector<std::wstring>& items);
void SetComboIndex(HWND combo, int index);
bool SelectComboText(HWND combo, const wchar_t* text);
int ComboFitWidth(HWND combo);
void FitDroppedWidth(HWND combo);

}
