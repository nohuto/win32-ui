// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/text/text.h"

#include "win32ui/window.h"

#include <atomic>
#include <string>

namespace win32ui
{
namespace
{

std::atomic<TextHook> g_hook = nullptr;

void TranslateWindowText(HWND window)
{
    const std::wstring text = WindowText(window);
    const wchar_t* translated = Text(text.c_str());
    if (translated != text.c_str())
    {
        SetWindowTextW(window, translated);
    }
}

}

void SetTextHook(TextHook hook) noexcept
{
    g_hook.store(hook, std::memory_order_release);
}

bool HasTextHook() noexcept
{
    return g_hook.load(std::memory_order_acquire) != nullptr;
}

const wchar_t* Text(const wchar_t* english) noexcept
{
    const TextHook hook = g_hook.load(std::memory_order_acquire);
    const wchar_t* text = hook && english ? hook(english) : nullptr;
    return text ? text : (english ? english : L"");
}

void TranslateWindowTexts(HWND window)
{
    if (!window || !HasTextHook())
    {
        return;
    }
    TranslateWindowText(window);
    EnumChildWindows(
        window,
        [](HWND child, LPARAM) -> BOOL {
            if (ClassIs(child, L"Static") || ClassIs(child, L"Button"))
            {
                TranslateWindowText(child);
            }
            return TRUE;
        },
        0
    );
}

}
