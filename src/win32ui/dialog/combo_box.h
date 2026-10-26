// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace win32ui
{

void SetComboItems(HWND combo, const std::vector<std::wstring>& items);
// width needed for the widest item and arrow, or 0 when empty
int ComboFitWidth(HWND combo);
// sizes the open list to its widest item, at least as wide as the combo box
void FitDroppedWidth(HWND combo);

} // namespace win32ui
