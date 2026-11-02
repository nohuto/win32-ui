// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <atomic>
#include <string>
#include <string_view>

namespace win32ui
{

using HttpHostCheck = bool (*)(std::wstring_view host, bool secure);

constexpr DWORD HttpStatusError(DWORD status) noexcept
{
    return static_cast<DWORD>(MAKE_HRESULT(SEVERITY_ERROR, FACILITY_HTTP, status));
}

// cancel aborts within about 100 ms
DWORD HttpGet(const std::wstring& url, const wchar_t* user_agent, HttpHostCheck allow, size_t max_bytes, const std::atomic_bool& cancel, std::string* body);

}
