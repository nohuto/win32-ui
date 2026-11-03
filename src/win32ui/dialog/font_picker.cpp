// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/dialog/font_picker.h"

#include "win32ui/dialog/dialog_window.h"
#include "win32ui/dialog/edit.h"
#include "win32ui/gdi/buffered_paint.h"
#include "win32ui/gdi/fonts.h"
#include "win32ui/handle_owner.h"
#include "win32ui/system/strings.h"
#include "win32ui/theme/theme_internal.h"
#include "win32ui/window.h"
#include "win32ui/window_metrics.h"

#include <commctrl.h>
#include <commdlg.h>
#include <dlgs.h>
#include <uxtheme.h>

#include <utility>

namespace win32ui
{

namespace
{

struct PickerState
{
    HWND dialog = nullptr;
    UniqueFont sample_font;
    LOGFONTW preview_base = {};
};

constexpr UINT_PTR kDialogSubclassId = 1;
constexpr UINT_PTR kSampleSubclassId = 4;
constexpr UINT kUpdatePreviewMessage = WM_APP + 101;

bool SameChoice(const LOGFONTW& left, const LOGFONTW& right)
{
    return EqualsInsensitive(left.lfFaceName, right.lfFaceName) && FontPointSize(left) == FontPointSize(right) && left.lfWeight == right.lfWeight && !!left.lfItalic == !!right.lfItalic;
}

void UpdateSample(HWND dialog, PickerState* state)
{
    HWND size_combo = GetDlgItem(dialog, cmb3);
    HWND sample = GetDlgItem(dialog, stc5);
    if (!state || !size_combo || !sample)
    {
        return;
    }
    LOGFONTW preview = state->preview_base;
    LOGFONTW selected = {};
    SendMessageW(dialog, WM_CHOOSEFONT_GETLOGFONT, 0, reinterpret_cast<LPARAM>(&selected));
    if (selected.lfFaceName[0] != L'\0')
    {
        wcsncpy_s(preview.lfFaceName, selected.lfFaceName, _TRUNCATE);
        preview.lfWeight = selected.lfWeight;
        preview.lfItalic = selected.lfItalic;
    }
    wchar_t size_text[32] = {};
    GetWindowTextW(size_combo, size_text, static_cast<int>(_countof(size_text)));
    if (const int point_size = _wtoi(size_text); point_size > 0)
    {
        preview.lfHeight = FontHeight(point_size, DpiForWindow(dialog));
    }
    UniqueFont font(CreateFontIndirectW(&preview));
    if (!font)
    {
        return;
    }
    ShowWindow(sample, SW_SHOWNA);
    SendMessageW(sample, WM_SETFONT, reinterpret_cast<WPARAM>(font.get()), TRUE);
    SetWindowTextW(sample, L"AaBbYyZz");
    InvalidateRect(sample, nullptr, TRUE);
    // previous font is deleted after the sample switched away from it
    state->sample_font = std::move(font);
}

void ThemeCombo(HWND combo)
{
    COMBOBOXINFO info = {sizeof(COMBOBOXINFO)};
    if (!combo || !GetComboBoxInfo(combo, &info))
    {
        return;
    }
    if (info.hwndItem)
    {
        SetThemeClass(info.hwndItem, L"DarkMode_CFD");
        InvalidateRect(info.hwndItem, nullptr, TRUE);
    }
    if (info.hwndList)
    {
        SetThemeClass(info.hwndList, L"DarkMode_Explorer");
        if ((GetWindowLongPtrW(combo, GWL_STYLE) & CBS_DROPDOWNLIST) != CBS_SIMPLE)
        {
            const LONG_PTR style = GetWindowLongPtrW(info.hwndList, GWL_STYLE);
            const LONG_PTR ex_style = GetWindowLongPtrW(info.hwndList, GWL_EXSTYLE);
            if (style & WS_BORDER)
            {
                SetWindowLongPtrW(info.hwndList, GWL_STYLE, style & ~WS_BORDER);
            }
            if (ex_style & WS_EX_CLIENTEDGE)
            {
                SetWindowLongPtrW(info.hwndList, GWL_EXSTYLE, ex_style & ~WS_EX_CLIENTEDGE);
            }
            SetWindowPos(info.hwndList, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        RedrawWindow(info.hwndList, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_FRAME);
    }
    InvalidateRect(combo, nullptr, TRUE);
}

// sample keeps the system window colors in every theme
LRESULT CALLBACK SampleProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR)
{
    switch (msg)
    {
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, SampleProc, id);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETTEXT:
    case WM_SETFONT:
        {
            const LRESULT result = DefSubclassProc(hwnd, msg, wparam, lparam);
            InvalidateRect(hwnd, nullptr, TRUE);
            return result;
        }
    case WM_PAINT:
        PaintBuffered(hwnd);
        return 0;
    case WM_PRINTCLIENT:
        {
            HDC hdc = reinterpret_cast<HDC>(wparam);
            RECT rect = {};
            GetClientRect(hwnd, &rect);
            FillRect(hdc, &rect, GetSysColorBrush(COLOR_WINDOW));
            HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
            HGDIOBJ old_font = font ? SelectObject(hdc, font) : nullptr;
            wchar_t text[128] = {};
            GetWindowTextW(hwnd, text, static_cast<int>(_countof(text)));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            DrawTextW(hdc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (old_font)
            {
                SelectObject(hdc, old_font);
            }
            return 0;
        }
    default:
        break;
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR)
{
    switch (msg)
    {
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, DialogProc, id);
        break;
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
        if (reinterpret_cast<HWND>(lparam) != GetDlgItem(hwnd, stc5))
        {
            return reinterpret_cast<LRESULT>(ThemeControlColor(msg, reinterpret_cast<HDC>(wparam), reinterpret_cast<HWND>(lparam)));
        }
        break;
    case WM_NOTIFY:
        if (LRESULT result = 0; ThemeCustomDraw(reinterpret_cast<NMHDR*>(lparam), &result))
        {
            return result;
        }
        break;
    default:
        break;
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

void InitDialog(HWND dialog, PickerState* state)
{
    state->dialog = dialog;
    SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
    EnsureSubclass(dialog, DialogProc, kDialogSubclassId);
    ApplyThemeToWindow(dialog);
    AddSelectAllShortcut(dialog);
    // once, a theme set again on every selection change costs a WM_THEMECHANGED each (B6-4)
    for (const int combo : {cmb1, cmb2, cmb3, cmb5})
    {
        ThemeCombo(GetDlgItem(dialog, combo));
    }
    if (HWND extra_bar = GetDlgItem(dialog, stc6))
    {
        ShowWindow(extra_bar, SW_HIDE);
    }
    if (HWND sample = GetDlgItem(dialog, stc5))
    {
        SetWindowTheme(sample, nullptr, nullptr);
        EnsureSubclass(sample, SampleProc, kSampleSubclassId);
    }
    PostMessageW(dialog, kUpdatePreviewMessage, 0, 0);
}

UINT_PTR CALLBACK HookProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    auto* state = reinterpret_cast<PickerState*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    switch (msg)
    {
    case WM_INITDIALOG:
        if (auto* choose = reinterpret_cast<CHOOSEFONTW*>(lparam))
        {
            InitDialog(hwnd, reinterpret_cast<PickerState*>(choose->lCustData));
        }
        return 0;
    case kUpdatePreviewMessage:
        UpdateSample(hwnd, state);
        return 0;
    case 0x02E0: // WM_DPICHANGED, not in the targeted SDK
        PostMessageW(hwnd, kUpdatePreviewMessage, 0, 0);
        break;
    case WM_COMMAND:
        if (!lparam)
        {
            break;
        }
        // drop-down list gets its border back when it opens
        if (HIWORD(wparam) == CBN_DROPDOWN)
        {
            ThemeCombo(reinterpret_cast<HWND>(lparam));
        }
        if (HIWORD(wparam) == CBN_SELCHANGE || HIWORD(wparam) == CBN_EDITCHANGE || HIWORD(wparam) == EN_CHANGE)
        {
            PostMessageW(hwnd, kUpdatePreviewMessage, 0, 0);
        }
        break;
    default:
        break;
    }
    return 0;
}

void CancelPicker(void* context)
{
    if (HWND dialog = static_cast<PickerState*>(context)->dialog)
    {
        PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    }
}

}

DWORD ChooseUiFont(HWND owner, const LOGFONTW& initial, const LOGFONTW& default_font, FontChoice* out)
{
    if (!out)
    {
        return ERROR_INVALID_PARAMETER;
    }
    LOGFONTW chosen = initial;
    PickerState state;
    state.preview_base = initial;
    CHOOSEFONTW choose = {};
    choose.lStructSize = sizeof(choose);
    choose.hwndOwner = owner;
    choose.lpLogFont = &chosen;
    choose.Flags = CF_ENABLEHOOK | CF_FORCEFONTEXIST | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS | CF_SCREENFONTS;
    choose.lpfnHook = HookProc;
    choose.lCustData = reinterpret_cast<LPARAM>(&state);
    BOOL accepted = FALSE;
    {
        const ModalScope modal(CancelPicker, &state);
        accepted = ChooseFontW(&choose);
    }
    if (!accepted)
    {
        return CommDlgExtendedError() == 0 ? ERROR_CANCELLED : ERROR_GEN_FAILURE;
    }
    out->use_default = SameChoice(chosen, default_font);
    out->font = out->use_default ? default_font : chosen;
    return ERROR_SUCCESS;
}

}
