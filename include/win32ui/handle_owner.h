// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

// each resource type has its own invalid value

#pragma once

#include <windows.h>

#include <objbase.h>

#include <utility>

namespace win32ui
{

template <typename Traits>
class UniqueResource
{
  public:
    using value_type = typename Traits::type;

    UniqueResource() noexcept = default;
    explicit UniqueResource(value_type value) noexcept
        : value_(value)
    {
    }
    ~UniqueResource()
    {
        reset();
    }
    UniqueResource(const UniqueResource&) = delete;
    UniqueResource& operator=(const UniqueResource&) = delete;
    UniqueResource(UniqueResource&& other) noexcept
        : value_(other.release())
    {
    }
    UniqueResource& operator=(UniqueResource&& other) noexcept
    {
        if (this != &other)
        {
            reset(other.release());
        }
        return *this;
    }

    value_type get() const noexcept
    {
        return value_;
    }
    // closes the current value, for APIs that return a new one through an out parameter
    value_type* put() noexcept
    {
        reset();
        return &value_;
    }
    value_type release() noexcept
    {
        return std::exchange(value_, Traits::invalid());
    }
    void reset(value_type value = Traits::invalid()) noexcept
    {
        if (Traits::valid(value_))
        {
            Traits::close(value_);
        }
        value_ = value;
    }
    explicit operator bool() const noexcept
    {
        return Traits::valid(value_);
    }

  private:
    value_type value_ = Traits::invalid();
};

// CreateEvent, CreateThread, OpenThread and OpenProcess return NULL on failure
// INVALID_HANDLE_VALUE is also the current process pseudo handle, which must never be closed
struct KernelHandleTraits
{
    using type = HANDLE;
    static HANDLE invalid() noexcept
    {
        return nullptr;
    }
    static bool valid(HANDLE handle) noexcept
    {
        return handle != nullptr && handle != INVALID_HANDLE_VALUE;
    }
    static void close(HANDLE handle) noexcept
    {
        CloseHandle(handle);
    }
};

// CreateFile returns INVALID_HANDLE_VALUE on failure
struct FileHandleTraits
{
    using type = HANDLE;
    static HANDLE invalid() noexcept
    {
        return INVALID_HANDLE_VALUE;
    }
    static bool valid(HANDLE handle) noexcept
    {
        return handle != nullptr && handle != INVALID_HANDLE_VALUE;
    }
    static void close(HANDLE handle) noexcept
    {
        CloseHandle(handle);
    }
};

// FindFirstFile returns INVALID_HANDLE_VALUE on failure and is closed with FindClose
struct FindHandleTraits
{
    using type = HANDLE;
    static HANDLE invalid() noexcept
    {
        return INVALID_HANDLE_VALUE;
    }
    static bool valid(HANDLE handle) noexcept
    {
        return handle != nullptr && handle != INVALID_HANDLE_VALUE;
    }
    static void close(HANDLE handle) noexcept
    {
        FindClose(handle);
    }
};

struct RegistryKeyTraits
{
    using type = HKEY;
    static HKEY invalid() noexcept
    {
        return nullptr;
    }
    static bool valid(HKEY key) noexcept
    {
        return key != nullptr;
    }
    static void close(HKEY key) noexcept
    {
        RegCloseKey(key);
    }
};

template <typename T>
struct GdiObjectTraits
{
    using type = T;
    static T invalid() noexcept
    {
        return nullptr;
    }
    static bool valid(T object) noexcept
    {
        return object != nullptr;
    }
    static void close(T object) noexcept
    {
        DeleteObject(object);
    }
};

// memory a Win32 API allocated with LocalAlloc, FormatMessage with FORMAT_MESSAGE_ALLOCATE_BUFFER for example
template <typename T>
struct LocalMemoryTraits
{
    using type = T;
    static T invalid() noexcept
    {
        return nullptr;
    }
    static bool valid(T memory) noexcept
    {
        return memory != nullptr;
    }
    static void close(T memory) noexcept
    {
        LocalFree(memory);
    }
};

// memory a COM API allocated with CoTaskMemAlloc, IShellItem::GetDisplayName for example
template <typename T>
struct CoTaskMemoryTraits
{
    using type = T;
    static T invalid() noexcept
    {
        return nullptr;
    }
    static bool valid(T memory) noexcept
    {
        return memory != nullptr;
    }
    static void close(T memory) noexcept
    {
        CoTaskMemFree(memory);
    }
};

using UniqueHandle = UniqueResource<KernelHandleTraits>;
using UniqueFile = UniqueResource<FileHandleTraits>;
using UniqueFind = UniqueResource<FindHandleTraits>;
using UniqueHKey = UniqueResource<RegistryKeyTraits>;

template <typename T>
using UniqueGdiObject = UniqueResource<GdiObjectTraits<T>>;

using UniqueFont = UniqueGdiObject<HFONT>;
using UniqueBrush = UniqueGdiObject<HBRUSH>;

template <typename T>
using UniqueLocal = UniqueResource<LocalMemoryTraits<T>>;

template <typename T>
using UniqueCoTask = UniqueResource<CoTaskMemoryTraits<T>>;

// CoUninitialize only after a successful CoInitializeEx, S_FALSE counts as success
class ComInit
{
  public:
    explicit ComInit(DWORD flags = COINIT_APARTMENTTHREADED) noexcept
        : hr_(CoInitializeEx(nullptr, flags))
    {
    }
    ~ComInit()
    {
        if (SUCCEEDED(hr_))
        {
            CoUninitialize();
        }
    }
    ComInit(const ComInit&) = delete;
    ComInit& operator=(const ComInit&) = delete;

    bool ok() const noexcept
    {
        return SUCCEEDED(hr_);
    }

  private:
    HRESULT hr_;
};

} // namespace win32ui
