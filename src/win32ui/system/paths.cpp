// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/system/paths.h"

#include "win32ui/handle_owner.h"
#include "win32ui/system/strings.h"
#include "win32ui/system_api.h"

#include <shlobj.h>

#include <algorithm>
#include <cwctype>

namespace win32ui
{
namespace
{

constexpr DWORD kMaxLongPath = 32768;

std::wstring_view NameOf(std::wstring_view path) noexcept
{
    const size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring_view::npos ? path : path.substr(separator + 1);
}

bool IsSeparator(std::wstring_view path, size_t index) noexcept
{
    return index < path.size() && (path[index] == L'\\' || path[index] == L'/');
}

bool IsDevicePath(std::wstring_view path) noexcept
{
    return path.size() >= 4 && IsSeparator(path, 0) && IsSeparator(path, 1) && (path[2] == L'?' || path[2] == L'.') && IsSeparator(path, 3);
}

// before Windows 11 "NUL.txt" and "NUL .txt" are still the NUL device
bool IsDeviceName(std::wstring_view name) noexcept
{
    std::wstring_view stem = name.substr(0, name.find(L'.'));
    while (!stem.empty() && stem.back() == L' ')
    {
        stem.remove_suffix(1);
    }
    if (stem.size() == 3)
    {
        return EqualsInsensitive(stem, L"CON") || EqualsInsensitive(stem, L"PRN") || EqualsInsensitive(stem, L"AUX") || EqualsInsensitive(stem, L"NUL");
    }
    const wchar_t digit = stem.size() == 4 ? stem[3] : 0;
    return (EqualsInsensitive(stem.substr(0, 3), L"COM") || EqualsInsensitive(stem.substr(0, 3), L"LPT")) && ((digit >= L'0' && digit <= L'9') || digit == L'\x00B9' || digit == L'\x00B2' || digit == L'\x00B3');
}

// drive, UNC, \\?\ and \\.\ roots like PathCchRemoveFileSpec, "\\.\C:" is the volume, GLOBALROOT device roots too
size_t RootLength(std::wstring_view path) noexcept
{
    const auto is_separator = [&](size_t index) { return IsSeparator(path, index); };
    // end of the second component from start ("server\share")
    const auto share_end = [&](size_t start) {
        const size_t server_end = path.find_first_of(L"\\/", start);
        const size_t end = server_end == std::wstring_view::npos ? server_end : path.find_first_of(L"\\/", server_end + 1);
        return end == std::wstring_view::npos ? path.size() : end;
    };
    if (IsDevicePath(path))
    {
        if (path.size() >= 8 && EqualsInsensitive(path.substr(4, 3), L"UNC") && is_separator(7))
        {
            return share_end(8);
        }
        // "\\?\GLOBALROOT\Device\HarddiskVolume1" is the volume, other GLOBALROOT paths have the plain device root
        if (path.size() > 22 && EqualsInsensitive(path.substr(4, 10), L"GLOBALROOT") && is_separator(14) && EqualsInsensitive(path.substr(15, 6), L"Device") &&
            is_separator(21) && !is_separator(22))
        {
            const size_t end = share_end(15);
            // "\\?\GLOBALROOT\Device\Mup\server\share" is "\\server\share"
            if (end == 25 && EqualsInsensitive(path.substr(22, 3), L"Mup") && is_separator(end))
            {
                return share_end(26);
            }
            return end < path.size() ? end + 1 : end;
        }
        const size_t end = path.find_first_of(L"\\/", 4);
        return end == std::wstring_view::npos ? path.size() : end + 1;
    }
    if (is_separator(0) && is_separator(1))
    {
        return share_end(2);
    }
    if (path.size() >= 2 && path[1] == L':')
    {
        return is_separator(2) ? 3 : 2;
    }
    return is_separator(0) ? 1 : 0;
}

}

std::wstring ModulePath(HMODULE module)
{
    std::wstring path;
    for (DWORD capacity = MAX_PATH;; capacity = std::min(capacity * 2, kMaxLongPath))
    {
        path.resize(capacity);
        const DWORD length = GetModuleFileNameW(module, path.data(), capacity);
        if (length == 0)
        {
            return {};
        }
        if (length < capacity)
        {
            path.resize(length);
            return path;
        }
        if (capacity == kMaxLongPath)
        {
            return {};
        }
    }
}

std::wstring ModuleDirectory()
{
    return ParentDirectory(ModulePath());
}

std::wstring FullPath(const std::wstring& path)
{
    std::wstring full(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD length = path.empty() ? 0 : GetFullPathNameW(path.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
        if (length == 0)
        {
            return path;
        }
        // needed size includes the null, the written length doesnt
        if (length < full.size())
        {
            full.resize(length);
            return full;
        }
        full.resize(length);
    }
}

std::wstring ParentDirectory(std::wstring_view path)
{
    const size_t root = RootLength(path);
    const size_t separator = path.find_last_of(L"\\/");
    return std::wstring(path.substr(0, separator == std::wstring_view::npos || separator < root ? root : separator));
}

std::wstring JoinPath(std::wstring_view left, std::wstring_view right)
{
    // right side with \\?\ or \\.\ is absolute and must keep its prefix (see below)
    if (left.empty() || IsDevicePath(right))
    {
        return std::wstring(right);
    }
    std::wstring path(left);
    // PathCchCombine only knows '\' as a separator
    if (path.back() == L'/')
    {
        path.back() = L'\\';
    }
    // PathCchCombine caps at MAX_PATH, drops \\?\ and reads the "." of \\.\ as a folder, those paths join below
    static const auto combine = ImportProc<HRESULT(WINAPI*)(PWSTR, size_t, PCWSTR, PCWSTR)>(L"kernelbase.dll", "PathCchCombine");
    wchar_t combined[MAX_PATH];
    if (combine && !IsDevicePath(left) && SUCCEEDED(combine(combined, MAX_PATH, path.c_str(), std::wstring(right).c_str())))
    {
        return combined;
    }
    // a rooted right side wins, a leading separator alone keeps the root of left
    const size_t right_root = RootLength(right);
    if (right_root > 1 || (right_root == 1 && RootLength(path) == 0))
    {
        return std::wstring(right);
    }
    if (right_root == 1)
    {
        path.resize(RootLength(path));
        right.remove_prefix(1);
    }
    if (path.back() != L'\\')
    {
        path.push_back(L'\\');
    }
    path.append(right);
    return path;
}

std::wstring FileName(std::wstring_view path)
{
    return std::wstring(NameOf(path));
}

std::wstring FileBaseName(std::wstring_view path)
{
    const std::wstring_view name = NameOf(path);
    const size_t dot = name.find_last_of(L'.');
    return std::wstring(dot == std::wstring_view::npos || dot == 0 ? name : name.substr(0, dot));
}

std::wstring TrimTrailingSeparators(std::wstring path)
{
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
    {
        path.pop_back();
    }
    return path;
}

bool HasFileExtension(std::wstring_view path, std::wstring_view extension) noexcept
{
    const std::wstring_view name = NameOf(path);
    const size_t dot = name.find_last_of(L'.');
    return dot != std::wstring_view::npos && EqualsInsensitive(name.substr(dot), extension);
}

std::wstring EnsureFileExtension(std::wstring path, std::wstring_view extension)
{
    if (!path.empty() && !HasFileExtension(path, extension))
    {
        path.append(extension);
    }
    return path;
}

std::wstring ReplaceFileExtension(std::wstring path, std::wstring_view extension)
{
    const size_t dot = path.rfind(L'.');
    const size_t separator = path.find_last_of(L"\\/");
    if (dot != std::wstring::npos && (separator == std::wstring::npos || dot > separator))
    {
        path.erase(dot);
    }
    return path.append(extension);
}

std::wstring SafeFileName(std::wstring_view name)
{
    std::wstring result;
    result.reserve(name.size());
    for (const wchar_t character : name)
    {
        result.push_back(character < 32 || std::wstring_view(L"<>:\"/\\|?*").find(character) != std::wstring_view::npos ? L'_' : character);
    }
    while (!result.empty() && (result.back() == L' ' || result.back() == L'.'))
    {
        result.pop_back();
    }
    result.erase(0, result.find_first_not_of(L' '));
    if (IsDeviceName(result))
    {
        result.insert(0, 1, L'_');
    }
    return result;
}

bool IsFile(const wchar_t* path) noexcept
{
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool IsDirectory(const wchar_t* path) noexcept
{
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool IsDriveAbsolute(std::wstring_view path) noexcept
{
    return path.size() >= 3 && iswalpha(path[0]) && path[1] == L':' && IsSeparator(path, 2);
}

bool OnFixedDrive(std::wstring_view path) noexcept
{
    if (!IsDriveAbsolute(path))
    {
        return false;
    }
    const wchar_t root[] = {path[0], L':', L'\\', L'\0'};
    const UINT type = GetDriveTypeW(root);
    return type == DRIVE_FIXED || type == DRIVE_RAMDISK;
}

bool IsMissing(const wchar_t* path) noexcept
{
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
    {
        return false;
    }
    const DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

DWORD EnsureDirectory(const wchar_t* path)
{
    const int result = SHCreateDirectoryExW(nullptr, path, nullptr);
    if (result != ERROR_ALREADY_EXISTS && result != ERROR_FILE_EXISTS)
    {
        return static_cast<DWORD>(result);
    }
    const DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        return GetLastError();
    }
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? ERROR_SUCCESS : ERROR_ALREADY_EXISTS;
}

std::wstring KnownFolderPath(const GUID& folder, DWORD flags, HANDLE token)
{
    UniqueCoTask<PWSTR> path;
    if (FAILED(SHGetKnownFolderPath(folder, flags, token, path.put())) || !path)
    {
        return {};
    }
    return path.get();
}

DWORD ForEachFile(const wchar_t* pattern, bool (*visit)(void* context, const WIN32_FIND_DATAW& data), void* context)
{
    WIN32_FIND_DATAW data;
    // no short names and larger batches (WinSxS 31.6 -> 8.6 ms), Vista rejects both flags
    UniqueFind find(FindFirstFileExW(pattern, FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH));
    if (!find && GetLastError() == ERROR_INVALID_PARAMETER)
    {
        find.reset(FindFirstFileW(pattern, &data));
    }
    if (!find)
    {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : error;
    }
    do
    {
        const wchar_t* name = data.cFileName;
        if (name[0] == L'.' && (name[1] == 0 || (name[1] == L'.' && name[2] == 0)))
        {
            continue;
        }
        if (!visit(context, data))
        {
            return ERROR_SUCCESS;
        }
    } while (FindNextFileW(find.get(), &data));
    const DWORD error = GetLastError();
    return error == ERROR_NO_MORE_FILES ? ERROR_SUCCESS : error;
}

}
