// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/system/registry_value.h"
#include "win32ui/system/registry_value_internal.h"

#include <cwchar>

namespace win32ui
{

DWORD ReadRegistryStringWith(RegGetValueFunction get_value, HKEY root, const wchar_t* subkey, const wchar_t* name, std::wstring* value, DWORD flags)
{
    value->clear();
    if (flags & RRF_RT_ANY)
    {
        return ERROR_INVALID_PARAMETER;
    }
    // without RRF_NOEXPAND RegGetValueW expands a REG_EXPAND_SZ value and reports it as REG_SZ
    const DWORD types = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | flags;
    // one call for most values, RegGetValueW reports the size it needs otherwise
    wchar_t buffer[256];
    DWORD size = sizeof(buffer);
    LSTATUS result = get_value(root, subkey, name, types, nullptr, buffer, &size);
    if (result == ERROR_SUCCESS)
    {
        value->assign(buffer, wcsnlen_s(buffer, size / sizeof(wchar_t)));
        return ERROR_SUCCESS;
    }
    // value can grow between two calls
    for (int attempt = 0; attempt < 3 && result == ERROR_MORE_DATA; ++attempt)
    {
        value->resize(size / sizeof(wchar_t) + 1);
        size = static_cast<DWORD>(value->size() * sizeof(wchar_t));
        result = get_value(root, subkey, name, types, nullptr, value->data(), &size);
        value->resize(result == ERROR_SUCCESS ? wcsnlen_s(value->c_str(), size / sizeof(wchar_t)) : 0);
    }
    return static_cast<DWORD>(result);
}

DWORD ReadRegistryString(HKEY root, const wchar_t* subkey, const wchar_t* name, std::wstring* value, DWORD flags)
{
    return ReadRegistryStringWith(RegGetValueW, root, subkey, name, value, flags);
}

DWORD ReadRegistryDword(HKEY root, const wchar_t* subkey, const wchar_t* name, DWORD* value, DWORD flags) noexcept
{
    if (flags & RRF_RT_ANY)
    {
        return ERROR_INVALID_PARAMETER;
    }
    DWORD data = 0;
    DWORD size = sizeof(data);
    const LSTATUS result = RegGetValueW(root, subkey, name, RRF_RT_REG_DWORD | flags, nullptr, &data, &size);
    if (result == ERROR_SUCCESS)
    {
        *value = data;
    }
    return static_cast<DWORD>(result);
}

DWORD WriteRegistryString(HKEY root, const wchar_t* subkey, const wchar_t* name, const wchar_t* value) noexcept
{
    const size_t length = std::wcslen(value);
    if (length >= MAXDWORD / sizeof(wchar_t))
    {
        return ERROR_INVALID_DATA;
    }
    return static_cast<DWORD>(RegSetKeyValueW(root, subkey, name, REG_SZ, value, static_cast<DWORD>((length + 1) * sizeof(wchar_t))));
}

}
