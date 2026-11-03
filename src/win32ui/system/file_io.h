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

DWORD ReadFileBytes(const wchar_t* path, std::vector<unsigned char>* bytes, uint64_t max_bytes);
DWORD ReadTextFile(const wchar_t* path, std::wstring* text, uint64_t max_bytes, bool ansi_fallback = true, TextEncoding* encoding = nullptr);
std::wstring FileReadErrorText(DWORD error);

std::wstring RandomFileSuffix(const wchar_t* extension);
// GetTempPath2W where it exists (SYSTEM gets SystemTemp)
DWORD TempDirectory(std::wstring* path);
DWORD CreateTempFolder(std::wstring_view prefix, std::wstring* path);
// waits briefly while another process holds path
DWORD WriteFileAtomic(const wchar_t* path, std::string_view bytes);
// unpaired surrogates give ERROR_NO_UNICODE_TRANSLATION in UTF-8, ansi isnt written
DWORD WriteTextFileAtomic(const wchar_t* path, std::wstring_view text, TextEncoding encoding = TextEncoding::utf8);

}
