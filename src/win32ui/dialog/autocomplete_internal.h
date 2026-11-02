// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/dialog/autocomplete.h"

#include <objidl.h>

namespace win32ui
{

// call on the thread of the edit, caller owns one reference
IEnumString* CreateSuggestSource(HWND edit, AutoCompleteSuggest suggest);

}
