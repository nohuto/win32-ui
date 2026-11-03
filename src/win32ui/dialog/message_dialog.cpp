// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/dialog/message_dialog.h"

#include "win32ui/dialog/dialog_layout.h"
#include "win32ui/dialog/dialog_layout_internal.h"
#include "win32ui/dialog/dialog_window.h"
#include "win32ui/dialog/tooltip.h"
#include "win32ui/gdi/icon_loader_internal.h"
#include "win32ui/handle_owner.h"
#include "win32ui/system/error_text.h"
#include "win32ui/system/shell_open.h"
#include "win32ui/system/strings.h"
#include "win32ui/text/text.h"
#include "win32ui/window_metrics.h"

#include <commctrl.h>

#include <algorithm>
#include <cstring>

namespace win32ui
{

namespace
{

constexpr int kMaxDetailLines = 10;
constexpr int kIconSize = 32;
constexpr int kBoxTextInset = 10;

struct IconCloser
{
    void operator()(HICON icon) const noexcept
    {
        DestroyIcon(icon);
    }
};

std::wstring_view TrimTrailingBreaks(std::wstring_view text)
{
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n'))
    {
        text.remove_suffix(1);
    }
    return text;
}

void SetClientWidth(HWND dialog, int width, UINT dpi)
{
    RECT client = {};
    GetClientRect(dialog, &client);
    RECT frame = {0, 0, width, client.bottom};
    AdjustWindowRectForDpi(&frame, static_cast<DWORD>(GetWindowLongPtrW(dialog, GWL_STYLE)), static_cast<DWORD>(GetWindowLongPtrW(dialog, GWL_EXSTYLE)), dpi);
    SetWindowPos(dialog, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

class MessageDialog final : public DialogWindow
{
  public:
    explicit MessageDialog(const MessageSpec& spec)
        : spec_(spec), details_(EditLineBreaks(TrimTrailingBreaks(spec.details))), multiline_(spec.wrap_details || details_.find(L'\n') != std::wstring::npos)
    {
    }

  protected:
    void OnCreate() override
    {
        SetWindowTextW(hwnd(), spec_.title.c_str());
        if (multiline_)
        {
            SetWindowLongPtrW(hwnd(), GWL_STYLE, GetWindowLongPtrW(hwnd(), GWL_STYLE) | WS_THICKFRAME);
        }
        client_width_ = Px(spec_.width_dip);
        if (spec_.outer_width_dip > 0)
        {
            RECT frame = {};
            AdjustWindowRectForDpi(&frame, static_cast<DWORD>(GetWindowLongPtrW(hwnd(), GWL_STYLE)), static_cast<DWORD>(GetWindowLongPtrW(hwnd(), GWL_EXSTYLE)), dpi());
            client_width_ = Px(spec_.outer_width_dip) - (frame.right - frame.left);
        }
        SetClientWidth(hwnd(), client_width_, dpi());
        if (spec_.icon)
        {
            icon_.reset(LoadSystemIcon(spec_.icon, kIconSize, dpi()));
            icon_control_ = GetDlgItem(hwnd(), kMessageIconId);
            SendMessageW(icon_control_, STM_SETICON, reinterpret_cast<WPARAM>(icon_.get()), 0);
            ShowWindow(icon_control_, SW_SHOWNA);
        }
        message_ = GetDlgItem(hwnd(), kMessageTextId);
        SetWindowTextW(message_, spec_.message.c_str());
        if (!details_.empty())
        {
            DWORD style = WS_TABSTOP | ES_READONLY;
            if (multiline_)
            {
                style |= ES_MULTILINE | ES_AUTOVSCROLL;
                // wrap at opening width, wider windows need fewer lines
                if (DetailLines(client_width_ - TextLeft() - Px(metrics::kDialogContentMargin)) > kMaxDetailLines)
                {
                    style |= WS_VSCROLL;
                }
            }
            else
            {
                style |= ES_AUTOHSCROLL;
            }
            // edit creation fails when text exceeds the default 32767 character limit
            details_box_ = CreateControl(hwnd(), WC_EDITW, L"", style, kMessageDetailsId);
            SendMessageW(details_box_, EM_SETLIMITTEXT, 0, 0);
            SetWindowTextW(details_box_, details_.c_str());
            SetWindowPos(details_box_, message_, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (!multiline_)
            {
                tooltip_ = win32ui::AddTooltip(hwnd(), details_box_, details_.c_str());
                if (tooltip_ && theme() && theme()->apply)
                {
                    theme()->apply(tooltip_);
                }
            }
            if (!spec_.copy_text.empty())
            {
                copy_ = GetDlgItem(hwnd(), kMessageCopyId);
                SetWindowTextW(copy_, spec_.copy_text.c_str());
                ShowWindow(copy_, SW_SHOWNA);
            }
        }
        for (const auto& button : spec_.buttons)
        {
            buttons_.push_back(CreateControl(hwnd(), WC_BUTTONW, button.text.c_str(), WS_TABSTOP | BS_PUSHBUTTON, button.id));
        }
        SendMessageW(hwnd(), DM_SETDEFID, static_cast<WPARAM>(spec_.default_id), 0);
    }

    HWND initial_focus() const override
    {
        return GetDlgItem(hwnd(), spec_.default_id);
    }

    void OnLayout(int width, int height) override
    {
        using namespace metrics;
        const int margin = Px(kDialogContentMargin);
        const int gap = Px(kBlockGap);
        const int button_height = TextPx(kButtonHeight);
        const int button_gap = Px(kButtonGap);
        const int bottom_margin = Px(kDialogButtonBottomMargin);

        std::vector<int> widths;
        int buttons_width = 0;
        for (size_t index = 0; index < buttons_.size(); ++index)
        {
            widths.push_back(std::max(Px(spec_.buttons[index].min_width_dip), TextFitWidth(buttons_[index])));
            buttons_width += widths.back() + (index ? button_gap : 0);
        }
        const int copy_width = copy_ ? std::max(Px(kButtonMinWidth), TextFitWidth(copy_)) : 0;
        if (GrowDialogWidth(hwnd(), margin + (copy_ ? copy_width + button_gap : 0) + buttons_width + Px(kDialogButtonRightMargin)))
        {
            return;
        }

        const int icon_size = icon_control_ ? Px(kIconSize) : 0;
        Place(icon_control_, margin, margin, icon_size, icon_size);
        const int text_x = TextLeft();
        const int text_width = std::max(0, width - text_x - margin);
        int y = margin;
        const int message_height = std::max(icon_size, TextHeight(hwnd(), fonts().ui(), spec_.message, text_width, DT_WORDBREAK));
        Place(message_, text_x, y, text_width, message_height);
        y += message_height + gap;
        if (details_box_)
        {
            const int lines = std::clamp(multiline_ ? DetailLines(text_width) : 1, 1, kMaxDetailLines);
            const int content = Px(kControlHeight - kDetailLineHeight) + lines * fonts().line_height();
            const int available = height - y - gap - button_height - bottom_margin;
            const int box_height = multiline_ && sized_ ? std::max(content, available) : content;
            Place(details_box_, text_x, y, text_width, box_height);
            y += box_height + gap;
            if (tooltip_)
            {
                SendMessageW(tooltip_, TTM_ACTIVATE, fonts().TextWidth(details_.c_str()) > text_width - Px(kBoxTextInset), 0);
            }
        }

        const int buttons_top = multiline_ && sized_ ? height - bottom_margin - button_height : y;
        int x = width - Px(kDialogButtonRightMargin) - buttons_width;
        for (size_t index = 0; index < buttons_.size(); ++index)
        {
            Place(buttons_[index], x, buttons_top, widths[index], button_height);
            x += widths[index] + button_gap;
        }
        Place(copy_, margin, buttons_top, copy_width, button_height);

        if (!multiline_ || !sized_)
        {
            sized_ = true;
            FitDialogHeight(hwnd(), buttons_top + button_height + bottom_margin);
        }
    }

    bool OnCommand(int id, int code, HWND control) override
    {
        if (code != BN_CLICKED)
        {
            return false;
        }
        if (control && control == copy_)
        {
            CopyTextToClipboard(hwnd(), spec_.message + L"\r\n\r\n" + details_);
            return true;
        }
        for (const auto& button : spec_.buttons)
        {
            if (button.id == id)
            {
                End(id);
                return true;
            }
        }
        if (id == IDCANCEL)
        {
            End(spec_.cancel_id);
            return true;
        }
        return false;
    }

  private:
    int TextLeft() const
    {
        return Px(metrics::kDialogContentMargin) + (spec_.icon ? Px(kIconSize) + Px(metrics::kBlockGap) : 0);
    }

    // more line breaks cannot change the count, long lines reach the cap through wrapping
    int DetailLines(int width) const
    {
        constexpr size_t kMeasuredCap = 16384;
        size_t end = 0;
        for (int breaks = 0; end < details_.size() && end < kMeasuredCap && breaks <= kMaxDetailLines; ++end)
        {
            breaks += details_[end] == L'\n' ? 1 : 0;
        }
        const int line = std::max(1, fonts().line_height());
        const int text = TextHeight(hwnd(), fonts().ui(), std::wstring_view(details_).substr(0, end), width - Px(kBoxTextInset), DT_WORDBREAK | DT_EDITCONTROL | DT_EXPANDTABS);
        return std::max(1, (text + line - 1) / line);
    }

    MessageSpec spec_;
    std::wstring details_;
    bool multiline_ = false;
    int client_width_ = 0;
    bool sized_ = false;
    UniqueNullable<HICON, IconCloser> icon_;
    HWND icon_control_ = nullptr;
    HWND message_ = nullptr;
    HWND details_box_ = nullptr;
    HWND tooltip_ = nullptr;
    HWND copy_ = nullptr;
    std::vector<HWND> buttons_;
};

class AboutDialog final : public DialogWindow
{
  public:
    explicit AboutDialog(const AboutSpec& spec)
        : spec_(spec)
    {
    }

  protected:
    void OnCreate() override
    {
        SetWindowTextW(hwnd(), spec_.title.c_str());
        SetClientWidth(hwnd(), Px(460), dpi());
        for (const auto& row : spec_.rows)
        {
            const bool link = row.find(L"<a ") != std::wstring::npos;
            rows_.push_back(CreateControl(hwnd(), link ? WC_LINK : WC_STATICW, row.c_str(), link ? WS_TABSTOP : SS_LEFT | SS_NOPREFIX, kAboutFirstRowId + static_cast<int>(rows_.size())));
        }
        ok_ = GetDlgItem(hwnd(), IDOK);
        SetWindowTextW(ok_, spec_.ok_text.empty() ? Text(L"OK") : spec_.ok_text.c_str());
        SetWindowPos(ok_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    HWND initial_focus() const override
    {
        return ok_;
    }

    LRESULT OnNotify(NMHDR* header, bool* handled) override
    {
        *handled = false;
        if ((header->code != NM_CLICK && header->code != NM_RETURN) || std::find(rows_.begin(), rows_.end(), header->hwndFrom) == rows_.end())
        {
            return 0;
        }
        const auto* link = reinterpret_cast<const NMLINK*>(header);
        if (link->item.szUrl[0] != L'\0')
        {
            if (const DWORD error = ShellOpen(hwnd(), link->item.szUrl); error != ERROR_SUCCESS && error != ERROR_CANCELLED)
            {
                MessageSpec failed;
                failed.title = Text(L"Error");
                failed.message = FormatWin32Error(error);
                failed.icon = IDI_ERROR;
                ShowMessage(hwnd(), failed);
            }
            *handled = true;
        }
        return 0;
    }

    void OnLayout(int width, int) override
    {
        using namespace metrics;
        const int padding = Px(kDialogContentMargin);
        const int line_height = TextPx(kCheckHeight);
        const int gap = Px(kRowGap);
        int text_width = 0;
        int y = padding;
        for (HWND row : rows_)
        {
            Place(row, padding, y, width - 2 * padding, line_height);
            text_width = std::max(text_width, TextFitWidth(row));
            y += line_height + gap;
        }
        const int button_y = y - gap + Px(kBlockGap);
        PlaceButtonRow({ok_}, width - Px(kDialogButtonRightMargin), button_y, Px(kButtonMinWidth), TextPx(kButtonHeight), 0);
        if (GrowDialogWidth(hwnd(), 2 * padding + text_width))
        {
            return;
        }
        FitDialogHeight(hwnd(), button_y + TextPx(kButtonHeight) + Px(kDialogButtonBottomMargin));
    }

  private:
    AboutSpec spec_;
    std::vector<HWND> rows_;
    HWND ok_ = nullptr;
};

}

INT_PTR ShowMessage(HWND owner, const MessageSpec& spec)
{
    MessageDialog dialog(spec);
    // a question that cant be shown must not count as yes or no
    const INT_PTR result = dialog.RunModal(owner, L"WIN32UI_MESSAGE");
    return result == -1 ? IDCANCEL : result;
}

void ShowAbout(HWND owner, const AboutSpec& spec)
{
    AboutDialog dialog(spec);
    dialog.RunModal(owner, L"WIN32UI_ABOUT");
}

void SplitMessageText(std::wstring_view text, std::wstring* message, std::wstring* details)
{
    const size_t split = text.find(L'\n');
    std::wstring_view first = text.substr(0, split);
    if (!first.empty() && first.back() == L'\r')
    {
        first.remove_suffix(1);
    }
    const size_t rest = split == std::wstring_view::npos ? std::wstring_view::npos : text.find_first_not_of(L"\r\n", split);
    message->assign(first);
    details->assign(rest == std::wstring_view::npos ? std::wstring_view() : text.substr(rest));
}

bool CopyTextToClipboard(HWND owner, std::wstring_view text)
{
    // clipboard is emptied only once the copy exists, a failed allocation keeps what was there
    const HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    void* locked = memory ? GlobalLock(memory) : nullptr;
    if (!locked)
    {
        const DWORD error = GetLastError();
        if (memory)
        {
            GlobalFree(memory);
        }
        SetLastError(error);
        return false;
    }
    std::memcpy(locked, text.data(), text.size() * sizeof(wchar_t));
    static_cast<wchar_t*>(locked)[text.size()] = L'\0';
    GlobalUnlock(memory);
    bool ok = false;
    DWORD error = ERROR_SUCCESS;
    if (OpenClipboard(owner))
    {
        ok = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        error = GetLastError();
        CloseClipboard();
    }
    else
    {
        error = GetLastError();
    }
    if (!ok)
    {
        GlobalFree(memory);
        SetLastError(error);
    }
    return ok;
}

}
