// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>
#include <string_view>
#include <type_traits>

namespace win32ui
{

std::wstring ModulePath(HMODULE module = nullptr);
std::wstring ModuleDirectory();
std::wstring FullPath(const std::wstring& path);
std::wstring ParentDirectory(std::wstring_view path);
std::wstring JoinPath(std::wstring_view left, std::wstring_view right);
std::wstring FileName(std::wstring_view path);
std::wstring FileBaseName(std::wstring_view path);
std::wstring TrimTrailingSeparators(std::wstring path);
bool HasFileExtension(std::wstring_view path, std::wstring_view extension) noexcept;
std::wstring EnsureFileExtension(std::wstring path, std::wstring_view extension);
std::wstring ReplaceFileExtension(std::wstring path, std::wstring_view extension);
std::wstring SafeFileName(std::wstring_view name);

bool IsFile(const wchar_t* path) noexcept;
bool IsDirectory(const wchar_t* path) noexcept;
bool IsMissing(const wchar_t* path) noexcept;
DWORD EnsureDirectory(const wchar_t* path);
std::wstring KnownFolderPath(const GUID& folder, DWORD flags = 0, HANDLE token = nullptr);

DWORD ForEachFile(const wchar_t* pattern, bool (*visit)(void* context, const WIN32_FIND_DATAW& data), void* context);

template <typename Visit>
DWORD ForEachFile(const wchar_t* pattern, Visit&& visit)
{
    using Callable = std::remove_reference_t<Visit>;
    return ForEachFile(
        pattern,
        [](void* context, const WIN32_FIND_DATAW& data) -> bool { return (*static_cast<Callable*>(context))(data); },
        const_cast<void*>(static_cast<const void*>(&visit))
    );
}

}
