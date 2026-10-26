// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

namespace win32ui
{

// uxtheme dark mode exports use ordinals, available from Windows 10 1809, null before
FARPROC UxthemeOrdinal(WORD ordinal);
// dark/custom status bars paint themselves, built in light keeps native painting
bool ThemeStatusBarCustom();

} // namespace win32ui
