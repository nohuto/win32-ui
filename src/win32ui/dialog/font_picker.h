// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

namespace win32ui
{

struct FontChoice
{
    bool use_default = true;
    LOGFONTW font = {};
};

// CommDlgExtendedError codes arent Win32 codes
DWORD ChooseUiFont(HWND owner, const LOGFONTW& initial, const LOGFONTW& default_font, FontChoice* out);

}
