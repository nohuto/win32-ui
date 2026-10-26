// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/gdi/fonts.h"

#include "win32ui/gdi/fonts_internal.h"
#include "win32ui/system_api.h"

#include <algorithm>
#include <cwchar>
#include <optional>

namespace win32ui
{

namespace
{

std::optional<LOGFONTW> g_custom_font;

int SystemFontDpi()
{
    HDC dc = GetDC(nullptr);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc)
    {
        ReleaseDC(nullptr, dc);
    }
    return dpi > 0 ? dpi : 96;
}

LOGFONTW NonClientFont(UINT dpi, LOGFONTW NONCLIENTMETRICSW::* member)
{
    dpi = dpi ? dpi : static_cast<UINT>(SystemFontDpi());
    static const auto metrics_for_dpi = ImportProc<BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT)>(L"user32.dll", "SystemParametersInfoForDpi");
    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    LOGFONTW font = {};
    if (metrics_for_dpi && metrics_for_dpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi))
    {
        font = metrics.*member;
    }
    else if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
    {
        // metrics use system DPI
        font = metrics.*member;
        font.lfHeight = MulDiv(font.lfHeight, static_cast<int>(dpi), SystemFontDpi());
    }
    else
    {
        font.lfHeight = FontHeight(9, dpi);
        font.lfWeight = FW_NORMAL;
        font.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(font.lfFaceName, L"Segoe UI");
    }
    return font;
}

} // namespace

void SetCustomFont(const LOGFONTW* font)
{
    g_custom_font = font ? std::optional<LOGFONTW>(*font) : std::nullopt;
}

LOGFONTW MessageFontLogFont(UINT dpi)
{
    return NonClientFont(dpi, &NONCLIENTMETRICSW::lfMessageFont);
}

LOGFONTW MenuFontLogFont(UINT dpi)
{
    return NonClientFont(dpi, &NONCLIENTMETRICSW::lfMenuFont);
}

LOGFONTW UIFontLogFont(UINT dpi)
{
    if (!g_custom_font)
    {
        return MessageFontLogFont(dpi);
    }
    LOGFONTW font = *g_custom_font;
    font.lfHeight = FontHeight(FontPointSize(font, 9), dpi ? dpi : static_cast<UINT>(SystemFontDpi()));
    return font;
}

int FontPointSize(const LOGFONTW& font, int zero_height_fallback)
{
    if (font.lfHeight == 0)
    {
        return zero_height_fallback;
    }
    return MulDiv(-font.lfHeight, 72, SystemFontDpi());
}

int FontHeight(int point_size)
{
    return -MulDiv(point_size, SystemFontDpi(), 72);
}

int FontHeight(int point_size, UINT dpi)
{
    return -MulDiv(point_size, static_cast<int>(dpi), 72);
}

void FontSet::Create(UINT dpi)
{
    dpi_ = dpi ? dpi : 96;
    // preserve font quality, message font follows Windows smoothing, custom fonts follow app settings
    const LOGFONTW base = UIFontLogFont(dpi_);
    ui_.reset(CreateFontIndirectW(&base));
    widths_.clear();
    grow_percent_ = 100;

    HDC dc = GetDC(nullptr);
    if (dc)
    {
        HGDIOBJ old = SelectObject(dc, ui_ ? ui_.get() : GetStockObject(DEFAULT_GUI_FONT));
        TEXTMETRICW tm = {};
        if (GetTextMetricsW(dc, &tm))
        {
            line_height_ = tm.tmHeight + tm.tmExternalLeading;
            average_char_width_ = std::max<int>(1, tm.tmAveCharWidth);
            // Segoe UI 9 pt has 16 pixel lines at 96 DPI, dialog units also follow text height
            grow_percent_ = std::max(100, MulDiv(line_height_, 100 * 96, 16 * static_cast<int>(dpi_)));
        }
        SelectObject(dc, old);
        ReleaseDC(nullptr, dc);
    }
}

int FontSet::TextWidth(const wchar_t* text) const
{
    if (!text || !*text)
    {
        return 0;
    }
    // screen DC lookup costs more than measurement, layouts repeatedly measure the same text
    const std::wstring_view view(text);
    if (const auto known = widths_.find(view); known != widths_.end())
    {
        return known->second;
    }
    std::wstring key(view);
    HDC dc = GetDC(nullptr);
    if (!dc)
    {
        return static_cast<int>(key.size()) * average_char_width_;
    }
    HGDIOBJ old = SelectObject(dc, ui_.get());
    SIZE size = {};
    GetTextExtentPoint32W(dc, key.c_str(), static_cast<int>(key.size()), &size);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    // changing labels would grow the cache without bound
    if (widths_.size() >= 512)
    {
        widths_.clear();
    }
    widths_.emplace(std::move(key), size.cx);
    return size.cx;
}

TEXTMETRICW WindowFontMetrics(HWND window)
{
    TEXTMETRICW metrics = {};
    HDC dc = GetDC(window);
    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
    const HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    GetTextMetricsW(dc, &metrics);
    if (old)
    {
        SelectObject(dc, old);
    }
    ReleaseDC(window, dc);
    return metrics;
}

} // namespace win32ui
