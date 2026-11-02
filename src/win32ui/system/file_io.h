// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace win32ui
{

enum class TextEncoding
{
    utf8,
    utf8_bom,
    utf16,
    ansi,
};

DWORD ReadFileBytes(const wchar_t* path, std::vector<unsigned char>* bytes, uint64_t max_bytes, DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
DWORD ReadTextFile(const wchar_t* path, std::wstring* text, uint64_t max_bytes, bool ansi_fallback = true, TextEncoding* encoding = nullptr);

std::wstring RandomFileSuffix(const wchar_t* extension);
// GetTempPath2W where it exists (SYSTEM gets SystemTemp)
DWORD TempDirectory(std::wstring* path);
DWORD CreateTempFolder(std::wstring_view prefix, std::wstring* path);
// waits briefly while another process holds path
DWORD WriteFileAtomic(const wchar_t* path, std::string_view bytes);
DWORD WriteTextFileAtomic(const wchar_t* path, std::wstring_view text, bool utf16 = false);

}
