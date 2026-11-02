// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <array>
#include <string_view>

namespace win32ui
{

using Sha256Digest = std::array<unsigned char, 32>;

DWORD Sha256(std::string_view bytes, Sha256Digest* digest);

}
