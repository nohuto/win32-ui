// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#define _CRT_RAND_S

#include "win32ui/system/file_io.h"

#include "win32ui/handle_owner.h"
#include "win32ui/system/error_text.h"
#include "win32ui/system/file_io_internal.h"
#include "win32ui/system/paths.h"
#include "win32ui/system/strings.h"
#include "win32ui/system_api.h"
#include "win32ui/text/text.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <optional>

namespace win32ui
{
namespace
{

constexpr DWORD kChunkBytes = 1u << 20;
constexpr int kTempAttempts = 16;
constexpr int kMoveRetries = 20;
constexpr DWORD kMoveRetryMs = 25;

DWORD WriteAll(HANDLE file, std::string_view bytes)
{
    while (!bytes.empty())
    {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size(), kChunkBytes));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data(), chunk, &written, nullptr))
        {
            return GetLastError();
        }
        if (written != chunk)
        {
            return ERROR_WRITE_FAULT;
        }
        bytes.remove_prefix(chunk);
    }
    return ERROR_SUCCESS;
}

// read-only files and directories never become replaceable, retries only help against scanners
bool Replaceable(const wchar_t* path)
{
    const DWORD attributes = GetFileAttributesW(path);
    return attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY)) == 0;
}

DWORD MoveOver(const wchar_t* from, const wchar_t* to)
{
    for (int retry = 0;; ++retry)
    {
        if (MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            return ERROR_SUCCESS;
        }
        const DWORD error = GetLastError();
        const bool transient = error == ERROR_SHARING_VIOLATION || (error == ERROR_ACCESS_DENIED && Replaceable(to));
        if (!transient || retry == kMoveRetries)
        {
            return error;
        }
        Sleep(kMoveRetryMs);
    }
}

DWORD WriteParts(const wchar_t* path, std::initializer_list<std::string_view> parts)
{
    for (int attempt = 0; attempt < kTempAttempts; ++attempt)
    {
        const std::wstring temp_path = path + RandomFileSuffix(L".tmp");
        // DELETE keeps scanners that dont share delete out, so a failed write can always remove the file
        const UniqueFile file(CreateFileW(temp_path.c_str(), GENERIC_WRITE | DELETE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file)
        {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_EXISTS)
            {
                continue;
            }
            return error;
        }
        DWORD error = ERROR_SUCCESS;
        for (const std::string_view part : parts)
        {
            error = error == ERROR_SUCCESS ? WriteAll(file.get(), part) : error;
        }
        if (error == ERROR_SUCCESS && !FlushFileBuffers(file.get()))
        {
            error = GetLastError();
        }
        if (error == ERROR_SUCCESS)
        {
            error = MoveOver(temp_path.c_str(), path);
        }
        if (error != ERROR_SUCCESS)
        {
            FILE_DISPOSITION_INFO dispose = {TRUE};
            SetFileInformationByHandle(file.get(), FileDispositionInfo, &dispose, sizeof(dispose));
        }
        return error;
    }
    return ERROR_FILE_EXISTS;
}

}

DWORD ReadFileBytes(const wchar_t* path, std::vector<unsigned char>* bytes, uint64_t max_bytes)
{
    bytes->clear();
    const UniqueFile file(CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    LARGE_INTEGER size = {};
    if (!file || !GetFileSizeEx(file.get(), &size))
    {
        return GetLastError();
    }
    if (static_cast<uint64_t>(size.QuadPart) > max_bytes || static_cast<uint64_t>(size.QuadPart) > bytes->max_size())
    {
        return ERROR_FILE_TOO_LARGE;
    }
    return ReadExactly(file.get(), static_cast<size_t>(size.QuadPart), bytes);
}

DWORD ReadExactly(HANDLE file, size_t size, std::vector<unsigned char>* bytes)
{
    bytes->resize(size);
    size_t offset = 0;
    while (offset < bytes->size())
    {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes->size() - offset, kChunkBytes));
        DWORD read = 0;
        if (!ReadFile(file, bytes->data() + offset, chunk, &read, nullptr))
        {
            const DWORD error = GetLastError();
            bytes->clear();
            return error;
        }
        // file shrank while reading, the bytes are no consistent copy
        if (read == 0)
        {
            bytes->clear();
            return ERROR_HANDLE_EOF;
        }
        offset += read;
    }
    return ERROR_SUCCESS;
}

std::wstring FileReadErrorText(DWORD error)
{
    switch (error)
    {
    case ERROR_FILE_TOO_LARGE:
        return Text(L"The file is too large.");
    case ERROR_INVALID_DATA:
        return Text(L"The file isn't valid text.");
    default:
        return FormatWin32Error(error);
    }
}

DWORD ReadTextFile(const wchar_t* path, std::wstring* text, uint64_t max_bytes, bool ansi_fallback, TextEncoding* encoding)
{
    text->clear();
    std::vector<unsigned char> bytes;
    // decoding takes int lengths
    if (const DWORD error = ReadFileBytes(path, &bytes, std::min<uint64_t>(max_bytes, INT_MAX)); error != ERROR_SUCCESS)
    {
        return error;
    }
    std::string_view view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    TextEncoding found = TextEncoding::utf16;
    if (view.starts_with("\xFF\xFE"))
    {
        if (view.size() % sizeof(wchar_t) != 0)
        {
            return ERROR_INVALID_DATA;
        }
        text->resize((view.size() - 2) / sizeof(wchar_t));
        std::memcpy(text->data(), view.data() + 2, view.size() - 2);
    }
    else
    {
        const bool bom = view.starts_with("\xEF\xBB\xBF");
        view.remove_prefix(bom ? 3 : 0);
        if (std::optional<std::wstring> wide = Utf8ToWide(view))
        {
            *text = std::move(*wide);
            found = bom ? TextEncoding::utf8_bom : TextEncoding::utf8;
        }
        else if (!bom && ansi_fallback)
        {
            *text = MultiByteToWide(view, CP_ACP);
            found = TextEncoding::ansi;
        }
        else
        {
            return ERROR_INVALID_DATA;
        }
    }
    if (encoding)
    {
        *encoding = found;
    }
    return ERROR_SUCCESS;
}

std::wstring RandomFileSuffix(const wchar_t* extension)
{
    unsigned high = 0;
    unsigned low = 0;
    if (rand_s(&high) != 0 || rand_s(&low) != 0)
    {
        high = GetTickCount();
        low = GetCurrentProcessId();
    }
    wchar_t suffix[20] = {};
    swprintf_s(suffix, L".%08x%08x", high, low);
    return std::wstring(suffix) + (extension ? extension : L"");
}

DWORD TempDirectory(std::wstring* path)
{
    static const auto temp_path2 = ImportProc<DWORD(WINAPI*)(DWORD, LPWSTR)>(L"kernel32.dll", "GetTempPath2W");
    std::wstring buffer(MAX_PATH + 1, L'\0');
    for (;;)
    {
        const DWORD size = static_cast<DWORD>(buffer.size());
        const DWORD length = temp_path2 ? temp_path2(size, buffer.data()) : GetTempPathW(size, buffer.data());
        if (length == 0)
        {
            return GetLastError();
        }
        if (length < size)
        {
            buffer.resize(length);
            *path = std::move(buffer);
            return ERROR_SUCCESS;
        }
        // too small, length is the size it needs
        buffer.resize(length);
    }
}

DWORD CreateTempFolder(std::wstring_view prefix, std::wstring* path)
{
    std::wstring temp;
    if (const DWORD error = TempDirectory(&temp))
    {
        return error;
    }
    for (int attempt = 0; attempt < kTempAttempts; ++attempt)
    {
        std::wstring candidate = JoinPath(temp, std::wstring(prefix) + RandomFileSuffix(L"").substr(1));
        if (CreateDirectoryW(candidate.c_str(), nullptr))
        {
            *path = std::move(candidate);
            return ERROR_SUCCESS;
        }
        if (GetLastError() != ERROR_ALREADY_EXISTS)
        {
            return GetLastError();
        }
    }
    return ERROR_ALREADY_EXISTS;
}

DWORD WriteFileAtomic(const wchar_t* path, std::string_view bytes)
{
    return WriteParts(path, {bytes});
}

DWORD WriteTextFileAtomic(const wchar_t* path, std::wstring_view text, TextEncoding encoding)
{
    if (encoding == TextEncoding::utf16)
    {
        return WriteParts(path, {std::string_view("\xFF\xFE", 2), std::string_view(reinterpret_cast<const char*>(text.data()), text.size() * sizeof(wchar_t))});
    }
    if (encoding == TextEncoding::ansi)
    {
        return ERROR_INVALID_PARAMETER;
    }
    const std::optional<std::string> bytes = WideToUtf8(text);
    if (!bytes)
    {
        return ERROR_NO_UNICODE_TRANSLATION;
    }
    return WriteParts(path, {encoding == TextEncoding::utf8_bom ? std::string_view("\xEF\xBB\xBF", 3) : std::string_view(), *bytes});
}

}
