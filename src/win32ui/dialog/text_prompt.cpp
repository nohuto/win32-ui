// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/dialog/text_prompt.h"

#include "win32ui/accessible_name.h"
#include "win32ui/dialog/dialog_layout.h"
#include "win32ui/dialog/dialog_layout_internal.h"
#include "win32ui/dialog/dialog_metrics.h"
#include "win32ui/dialog/dialog_window.h"
#include "win32ui/dialog/edit.h"
#include "win32ui/text/text.h"
#include "win32ui/window.h"
#include "win32ui/window_metrics.h"

#include <commctrl.h>

#include <algorithm>
#include <utility>

namespace win32ui
{

namespace
{

constexpr int kLabelId = 2201;
constexpr int kEditId = 2202;
constexpr int kBrowseId = 2203;
constexpr int kOneLineWidthDip = 385;
constexpr int kMultilineWidthDip = 595;
constexpr int kMultilineHeightDip = 380;
constexpr int kLabelGapDip = 4;

class TextPromptDialog final : public DialogWindow
{
  public:
    TextPromptDialog(const TextPrompt& prompt, std::wstring text)
        : title_(prompt.title), label_(prompt.label), multiline_(prompt.multiline), browse_(prompt.browse), context_(prompt.context), text_(std::move(text))
    {
        // resizable prompt keeps its opening size as the minimum
        min_width_dip_ = multiline_ ? 0 : min_width_dip_;
        min_height_dip_ = multiline_ ? 0 : min_height_dip_;
    }

    std::wstring& text()
    {
        return text_;
    }

  protected:
    HWND initial_focus() const override
    {
        return edit_;
    }

    void OnCreate() override
    {
        SetWindowTextW(hwnd(), title_.c_str());
        if (multiline_)
        {
            SetWindowLongPtrW(hwnd(), GWL_STYLE, GetWindowLongPtrW(hwnd(), GWL_STYLE) | WS_THICKFRAME);
        }
        RECT frame = {0, 0, Px(multiline_ ? kMultilineWidthDip : kOneLineWidthDip), Px(multiline_ ? kMultilineHeightDip : metrics::kControlHeight)};
        AdjustWindowRectForDpi(&frame, static_cast<DWORD>(GetWindowLongPtrW(hwnd(), GWL_STYLE)), static_cast<DWORD>(GetWindowLongPtrW(hwnd(), GWL_EXSTYLE)), dpi());
        SetWindowPos(hwnd(), nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

        label_box_ = CreateControl(hwnd(), WC_STATICW, label_.c_str(), SS_NOPREFIX, kLabelId);
        const DWORD style = WS_TABSTOP | (multiline_ ? ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL : ES_AUTOHSCROLL);
        edit_ = CreateControl(hwnd(), WC_EDITW, L"", style, kEditId);
        SendMessageW(edit_, EM_SETLIMITTEXT, 0, 0);
        SetWindowTextW(edit_, text_.c_str());
        SendMessageW(edit_, EM_SETSEL, 0, -1);
        SetAccessibleName(edit_, label_);
        if (!multiline_)
        {
            KeepSingleLine(edit_);
        }
        if (browse_)
        {
            browse_button_ = CreateControl(hwnd(), WC_BUTTONW, Text(L"Browse..."), WS_TABSTOP | BS_PUSHBUTTON, kBrowseId);
        }
        ok_ = CreateControl(hwnd(), WC_BUTTONW, Text(L"OK"), WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK);
        cancel_ = CreateControl(hwnd(), WC_BUTTONW, Text(L"Cancel"), WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL);
        SendMessageW(hwnd(), DM_SETDEFID, IDOK, 0);
    }

    void OnLayout(int width, int height) override
    {
        using namespace metrics;
        const int margin = Px(kDialogContentMargin);
        const int gap = Px(kBlockGap);
        const int button_height = TextPx(kButtonHeight);
        const int bottom_margin = Px(kDialogButtonBottomMargin);
        const int right = width - Px(kDialogButtonRightMargin);
        const int button_min = Px(kButtonMinWidth);
        const int browse_width = browse_button_ ? std::max(button_min, TextFitWidth(browse_button_)) : 0;
        const int buttons_width = std::max(button_min, TextFitWidth(ok_)) + Px(kButtonGap) + std::max(button_min, TextFitWidth(cancel_));
        if (GrowDialogWidth(hwnd(), margin + (browse_width ? browse_width + Px(kButtonGap) : 0) + buttons_width + Px(kDialogButtonRightMargin)))
        {
            return;
        }

        const int text_width = std::max(0, width - margin * 2);
        int y = margin;
        const int label_height = TextHeight(hwnd(), fonts().ui(), label_, text_width, DT_WORDBREAK);
        Place(label_box_, margin, y, text_width, label_height);
        y += label_height + Px(kLabelGapDip);
        const int one_line_buttons = y + TextPx(kControlHeight) + gap;
        const int buttons_top = multiline_ ? std::max(one_line_buttons, height - bottom_margin - button_height) : one_line_buttons;
        Place(edit_, margin, y, text_width, buttons_top - gap - y);
        PlaceButtonRow({ok_, cancel_}, right, buttons_top, button_min, button_height, Px(kButtonGap));
        Place(browse_button_, margin, buttons_top, browse_width, button_height);
        if (!multiline_)
        {
            FitDialogHeight(hwnd(), buttons_top + button_height + bottom_margin);
        }
    }

    bool OnCommand(int id, int code, HWND) override
    {
        if (code != BN_CLICKED)
        {
            return false;
        }
        if (id == IDOK)
        {
            text_ = WindowText(edit_);
            End(IDOK);
            return true;
        }
        if (id == kBrowseId && browse_)
        {
            AppendBrowsed();
            return true;
        }
        return false;
    }

  private:
    void AppendBrowsed()
    {
        std::wstring line;
        if (!browse_(context_, hwnd(), &line) || line.empty())
        {
            return;
        }
        std::wstring text = WindowText(edit_);
        if (!text.empty() && text.back() != L'\n' && text.back() != L'\r')
        {
            text += L"\r\n";
        }
        text += line;
        SetWindowTextW(edit_, text.c_str());
        SendMessageW(edit_, EM_SETSEL, text.size(), text.size());
        SetFocus(edit_);
    }

    std::wstring title_;
    std::wstring label_;
    bool multiline_ = false;
    bool (*browse_)(void* context, HWND owner, std::wstring* line) = nullptr;
    void* context_ = nullptr;
    std::wstring text_;
    HWND label_box_ = nullptr;
    HWND edit_ = nullptr;
    HWND browse_button_ = nullptr;
    HWND ok_ = nullptr;
    HWND cancel_ = nullptr;
};

}

DWORD PromptText(HWND owner, const TextPrompt& prompt, std::wstring* text)
{
    if (!text)
    {
        return ERROR_INVALID_PARAMETER;
    }
    TextPromptDialog dialog(prompt, *text);
    if (const INT_PTR result = dialog.RunModal(owner, L"WIN32UI_TEXT_PROMPT"); result != IDOK)
    {
        return result == -1 && GetLastError() ? GetLastError() : ERROR_CANCELLED;
    }
    *text = std::move(dialog.text());
    return 0;
}

}
