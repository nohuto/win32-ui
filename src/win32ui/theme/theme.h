// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/dialog/dialog_layout.h"
#include "win32ui/list_view/list_view.h"
#include "win32ui/theme/palette.h"

#include <windows.h>

#include <commctrl.h>

#include <vector>

namespace win32ui
{

enum class TextRole
{
    normal,
    error,
    disabled,
};

void ApplyThemeToWindow(HWND window);
void ReapplyTheme(HWND top_level);
void ReapplyFonts(HWND root, HFONT font);
HBRUSH ThemeControlColor(UINT message, HDC hdc, HWND control);
bool ThemeCustomDraw(NMHDR* header, LRESULT* result);
void SetTextRole(HWND label, TextRole role);

enum DialogPrepare : unsigned
{
    kPrepareTranslate = 1,   // TranslateWindowTexts
    kPrepareNoTextLimit = 2, // edits and combo boxes take any length
    kPrepareSingleLine = 4,  // KeepSingleLine for writable ES_MULTILINE edits
};

// returns static storage
const DialogTheme* ThemeDialogHooks(unsigned prepare = 0);
ListViewHooks DialogListHooks(bool (*row_menu)(void* context, HWND list, int row, POINT screen) = nullptr);
// hooks must outlive the list
bool SetupDialogList(HWND list, const ListViewHooks* hooks, DWORD extra_styles = 0, std::vector<ListColumn> columns = {});

}
