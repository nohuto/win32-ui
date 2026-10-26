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

// message and About control IDs from message_dialog.rc, also used as UI Automation AutomationIds
inline constexpr int kMessageTextId = 2001;
inline constexpr int kMessageDetailsId = 2002;
inline constexpr int kMessageCopyId = 2003;
inline constexpr int kMessageIconId = 2004;
inline constexpr int kAboutFirstRowId = 2010;

struct MessageButton
{
    int id = IDOK;
    std::wstring text;
    // wider when the text needs it
    int min_width_dip = metrics::kButtonMinWidth;
};

struct MessageSpec
{
    std::wstring title;
    std::wstring message;
    // single line text uses a tooltip when truncated
    // multiline text wraps up to 10 visible lines, then scrolls, dialog is resizable and box fills available space
    std::wstring details;
    // left to right
    std::vector<MessageButton> buttons;
    int default_id = IDOK;
    // Escape and close result
    int cancel_id = IDCANCEL;
    // system icon such as IDI_WARNING, or null for none
    PCWSTR icon = nullptr;
    // nonempty text shows a leftmost button that copies message and details
    std::wstring copy_text;
    int width_dip = 460;
};

// returns pressed button id, or cancel_id for Escape or close
// app RC must include <win32ui/dialog/message_dialog.rc>
INT_PTR ShowMessage(HWND owner, const MessageSpec& spec);

struct AboutSpec
{
    std::wstring title;
    // one row per entry, <a href="..."> markup opens links with the shell
    std::vector<std::wstring> rows;
    std::wstring ok_text;
};

void ShowAbout(HWND owner, const AboutSpec& spec);

bool CopyTextToClipboard(HWND owner, std::wstring_view text);

} // namespace win32ui
