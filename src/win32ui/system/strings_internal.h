// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <string_view>

namespace win32ui
{

int CompareOrdinalPieces(std::wstring_view left, std::wstring_view right, size_t piece) noexcept;
size_t FindInsensitiveScan(std::wstring_view text, std::wstring_view needle) noexcept;

}
