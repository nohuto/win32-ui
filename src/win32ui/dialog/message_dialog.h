// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/dialog/dialog_metrics.h"

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

namespace win32ui
{

inline constexpr int kMessageTextId = 2001;
inline constexpr int kMessageDetailsId = 2002;
inline constexpr int kMessageCopyId = 2003;
inline constexpr int kMessageIconId = 2004;
inline constexpr int kAboutFirstRowId = 2010;

struct MessageButton
{
    int id = IDOK;
    std::wstring text;
    int min_width_dip = metrics::kButtonMinWidth;
};

struct MessageSpec
{
    std::wstring title;
    std::wstring message;
    std::wstring details;
    std::vector<MessageButton> buttons;
    int default_id = IDOK;
    int cancel_id = IDCANCEL;
    PCWSTR icon = nullptr;
    std::wstring copy_text;
    int width_dip = 460;
    int outer_width_dip = 0;
    bool wrap_details = false;
};

INT_PTR ShowMessage(HWND owner, const MessageSpec& spec);
void SplitMessageText(std::wstring_view text, std::wstring* message, std::wstring* details);

struct AboutSpec
{
    std::wstring title;
    std::vector<std::wstring> rows;
    std::wstring ok_text;
};

void ShowAbout(HWND owner, const AboutSpec& spec);

// CF_UNICODETEXT readers stop at an embedded NUL, GetLastError has the reason of a failure
bool CopyTextToClipboard(HWND owner, std::wstring_view text);

}
