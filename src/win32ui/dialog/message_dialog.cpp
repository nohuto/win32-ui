// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/dialog/message_dialog.h"

#include "win32ui/dialog/dialog_layout.h"
#include "win32ui/dialog/dialog_layout_internal.h"
#include "win32ui/dialog/dialog_window.h"
#include "win32ui/gdi/icon_loader_internal.h"
#include "win32ui/handle_owner.h"

#include <commctrl.h>
#include <shellapi.h>

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

std::wstring EditLineBreaks(std::wstring_view text)
{
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n'))
    {
        text.remove_suffix(1);
    }
    std::wstring result;
    result.reserve(text.size());
    for (wchar_t ch : text)
    {
        if (ch == L'\r')
        {
            continue;
        }
        if (ch == L'\n')
        {
            result.push_back(L'\r');
        }
        result.push_back(ch);
    }
    return result;
}

int TextHeight(HWND window, HFONT font, const std::wstring& text, int width, UINT format)
{
    HDC dc = GetDC(window);
    if (!dc)
    {
        return 0;
    }
    HGDIOBJ old = SelectObject(dc, font);
    RECT rect = {0, 0, std::max(1, width), 0};
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, DT_CALCRECT | DT_NOPREFIX | format);
    SelectObject(dc, old);
    ReleaseDC(window, dc);
    return rect.bottom;
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
        : spec_(spec), details_(EditLineBreaks(spec.details)), multiline_(details_.find(L'\n') != std::wstring::npos)
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
        SetClientWidth(hwnd(), Px(spec_.width_dip), dpi());
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
                if (DetailLines(Px(spec_.width_dip) - TextLeft() - Px(metrics::kDialogContentMargin)) > kMaxDetailLines)
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
                AddTooltip();
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

        // fit buttons before positioning controls
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

    // measure wrapped details at width, capped at kMaxDetailLines
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
        const int text = TextHeight(hwnd(), fonts().ui(), details_.substr(0, end), width - Px(kBoxTextInset), DT_WORDBREAK | DT_EDITCONTROL | DT_EXPANDTABS);
        return std::max(1, (text + line - 1) / line);
    }

    // full text for a truncated single line box
    void AddTooltip()
    {
        tooltip_ = CreateTooltip(hwnd());
        if (!tooltip_)
        {
            return;
        }
        TOOLINFOW info = {};
        info.cbSize = sizeof(info);
        info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        info.hwnd = hwnd();
        info.uId = reinterpret_cast<UINT_PTR>(details_box_);
        info.lpszText = details_.data();
        SendMessageW(tooltip_, TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&info));
        SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, Px(600));
        if (theme() && theme()->apply)
        {
            theme()->apply(tooltip_);
        }
    }

    MessageSpec spec_;
    std::wstring details_;
    bool multiline_ = false;
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
        SetWindowTextW(ok_, spec_.ok_text.c_str());
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
            ShellExecuteW(hwnd(), L"open", link->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL);
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

} // namespace

INT_PTR ShowMessage(HWND owner, const MessageSpec& spec)
{
    MessageDialog dialog(spec);
    return dialog.RunModal(owner, L"WIN32UI_MESSAGE");
}

void ShowAbout(HWND owner, const AboutSpec& spec)
{
    AboutDialog dialog(spec);
    dialog.RunModal(owner, L"WIN32UI_ABOUT");
}

bool CopyTextToClipboard(HWND owner, std::wstring_view text)
{
    if (!OpenClipboard(owner))
    {
        return false;
    }
    bool ok = false;
    EmptyClipboard();
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t)))
    {
        if (void* locked = GlobalLock(memory))
        {
            std::memcpy(locked, text.data(), text.size() * sizeof(wchar_t));
            static_cast<wchar_t*>(locked)[text.size()] = L'\0';
            GlobalUnlock(memory);
            ok = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        }
        if (!ok)
        {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
    return ok;
}

} // namespace win32ui
