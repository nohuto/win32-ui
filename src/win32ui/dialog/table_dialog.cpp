// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/dialog/table_dialog.h"

#include "win32ui/dialog/dialog_layout.h"
#include "win32ui/dialog/dialog_metrics.h"
#include "win32ui/dialog/dialog_window.h"
#include "win32ui/dialog/message_dialog.h"
#include "win32ui/list_view/list_view.h"
#include "win32ui/menu/popup_menu.h"
#include "win32ui/text/text.h"
#include "win32ui/theme/theme.h"
#include "win32ui/window.h"
#include "win32ui/window_metrics.h"

#include <commctrl.h>

#include <algorithm>

namespace win32ui
{

namespace
{

constexpr int kIdentifierId = 2301;
constexpr int kActionId = 2302;
constexpr size_t kInitialRows = 10;

struct State
{
    const TablesSpec* spec = nullptr;
    std::vector<HWND> titles;
    std::vector<HWND> lists;
    std::vector<int> heights;
    std::vector<int> sort_columns;
    std::vector<bool> sort_ascending;
    int title_height = 0;
    UINT layout_dpi = 0;
    int right_margin_dip = 0;
    int bottom_offset_dip = 0;
    int min_width_dip = 0;
    int min_height_dip = 0;
    bool ready = false;
};

bool HasChecks(const State& state, size_t table)
{
    return state.spec->on_check && state.spec->check_table >= 0 && table == static_cast<size_t>(state.spec->check_table);
}

void AppendLine(std::wstring* text, const std::vector<std::wstring>& cells)
{
    for (size_t index = 0; index < cells.size(); ++index)
    {
        text->append(index ? L"\t" : L"").append(cells[index]);
    }
    text->append(L"\r\n");
}

std::wstring TablesText(const TablesSpec& spec)
{
    std::wstring text = std::wstring(spec.identifier) + L"\r\n";
    for (const TextTable& table : spec.tables)
    {
        text.append(L"\r\n").append(table.title).append(L"\r\n");
        AppendLine(&text, table.columns);
        for (const auto& row : table.rows)
        {
            AppendLine(&text, row);
        }
    }
    return text;
}

bool CheckRowMenu(void*, HWND list, int row, POINT screen)
{
    const HWND dialog = GetParent(list);
    const auto* state = reinterpret_cast<const State*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (!state)
    {
        return false;
    }
    const auto found = std::find(state->lists.begin(), state->lists.end(), list);
    if (found == state->lists.end() || !HasChecks(*state, static_cast<size_t>(found - state->lists.begin())))
    {
        return false;
    }
    // empty space menus target the selected row
    row = row >= 0 ? row : ListView_GetNextItem(list, -1, LVNI_SELECTED);
    constexpr int kMenuCheck = 1;
    HMENU menu = CreatePopupMenu();
    if (!menu)
    {
        return true;
    }
    const bool checked = row >= 0 && ListView_GetCheckState(list, row);
    if (row >= 0)
    {
        // row text is a label, not a mnemonic or a shortcut column
        std::wstring label = MenuName(ListCellText(list, row, 0));
        std::replace(label.begin(), label.end(), L'\t', L' ');
        AppendMenuItem(menu, MF_STRING | (checked ? MF_CHECKED : 0), kMenuCheck, label.c_str(), L"Space");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendListRowCommands(menu, list);
    const int chosen = TrackContextMenu(menu, screen, dialog);
    if (chosen == kMenuCheck)
    {
        ListView_SetCheckState(list, row, !checked);
    }
    else
    {
        RunListRowCommand(list, static_cast<UINT>(chosen));
    }
    return true;
}

RECT ControlRect(HWND dialog, int id)
{
    RECT rect = {};
    ChildRect(dialog, GetDlgItem(dialog, id), &rect);
    return rect;
}

int Gap(HWND dialog)
{
    return Scale(metrics::kBlockGap, DpiForWindow(dialog)) / 2;
}

void Layout(HWND dialog, State* state)
{
    if (!state->layout_dpi)
    {
        return;
    }
    const UINT dpi = DpiForWindow(dialog);
    if (state->layout_dpi != dpi)
    {
        state->title_height = MulDiv(state->title_height, static_cast<int>(dpi), static_cast<int>(state->layout_dpi));
        for (int& height : state->heights)
        {
            height = MulDiv(height, static_cast<int>(dpi), static_cast<int>(state->layout_dpi));
        }
        state->layout_dpi = dpi;
    }
    RECT client = {};
    GetClientRect(dialog, &client);
    RECT top = ControlRect(dialog, kIdentifierId);
    RECT bottom = ControlRect(dialog, IDOK);
    // anchors apply after the procedure handles WM_SIZE
    top.right = client.right - Scale(state->right_margin_dip, dpi);
    bottom.top = client.bottom - Scale(state->bottom_offset_dip, dpi);
    const int gap = Gap(dialog);
    const size_t count = state->lists.size();
    const int space = bottom.top - top.bottom - gap - static_cast<int>(count) * (state->title_height + gap);
    const std::vector<int> heights = ShareSpace(state->heights, space);
    const int width = top.right - top.left;
    int y = top.bottom + gap;
    for (size_t i = 0; i < count; ++i)
    {
        Place(state->titles[i], top.left, y, width, state->title_height);
        y += state->title_height;
        Place(state->lists[i], top.left, y, width, heights[i]);
        y += heights[i] + gap;
    }
}

int FillList(HWND list, const TextTable& table)
{
    int width = 0;
    std::vector<ListColumn> columns;
    for (const std::wstring& title : table.columns)
    {
        columns.push_back({title});
    }
    SetListColumns(list, columns);
    SetListRows(list, table.rows);
    for (size_t column = 0; column < table.columns.size(); ++column)
    {
        width += FitListColumn(list, static_cast<int>(column));
    }
    // Reset columns restores fitted widths
    columns = ListColumns(list);
    for (ListColumn& column : columns)
    {
        column.default_width_dip = column.width_dip;
    }
    SetListColumns(list, columns);
    const UINT dpi = DpiForWindow(list);
    return width + SystemMetricForDpi(SM_CXVSCROLL, dpi) + 2 * SystemMetricForDpi(SM_CXEDGE, dpi);
}

void CreateLists(HWND dialog, State* state)
{
    TEXTMETRICW text_metrics = {};
    HDC dc = GetDC(dialog);
    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(dialog, WM_GETFONT, 0, 0));
    const HGDIOBJ previous = SelectObject(dc, font);
    GetTextMetricsW(dc, &text_metrics);
    SelectObject(dc, previous);
    ReleaseDC(dialog, dc);
    state->title_height = text_metrics.tmHeight + text_metrics.tmExternalLeading + 2;
    const RECT top = ControlRect(dialog, kIdentifierId);
    int content_width = 0;
    int content_height = 0;
    static const ListViewHooks hooks = DialogListHooks(CheckRowMenu);
    for (const TextTable& table : state->spec->tables)
    {
        HWND title = CreateControl(dialog, WC_STATICW, table.title.c_str(), SS_LEFT | SS_NOPREFIX, -1);
        HWND list = CreateControl(dialog, WC_LISTVIEWW, L"", WS_TABSTOP | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SHOWSELALWAYS, 0);
        for (HWND control : {title, list})
        {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        }
        const bool checks = HasChecks(*state, state->lists.size());
        SetupDialogList(list, &hooks, checks ? LVS_EX_CHECKBOXES : 0);
        SetWindowPos(list, nullptr, 0, 0, top.right - top.left, top.bottom - top.top, SWP_NOZORDER | SWP_NOACTIVATE);
        content_width = std::max(content_width, FillList(list, table));
        for (size_t row = 0; checks && row < state->spec->checked.size() && row < table.rows.size(); ++row)
        {
            ListView_SetCheckState(list, static_cast<int>(row), state->spec->checked[row]);
        }
        RECT window = {};
        RECT client = {};
        RECT header = {};
        RECT item = {};
        GetWindowRect(list, &window);
        GetClientRect(list, &client);
        GetWindowRect(ListView_GetHeader(list), &header);
        ListView_GetItemRect(list, 0, &item, LVIR_BOUNDS);
        const int chrome = (window.bottom - window.top - client.bottom) + (header.bottom - header.top);
        const int row = item.bottom - item.top;
        state->heights.push_back(chrome + static_cast<int>(table.rows.size()) * row);
        content_height += chrome + static_cast<int>(std::min(table.rows.size(), kInitialRows)) * row + state->title_height;
        state->titles.push_back(title);
        state->lists.push_back(list);
        state->sort_columns.push_back(-1);
        state->sort_ascending.push_back(true);
    }
    const RECT bottom = ControlRect(dialog, IDOK);
    RECT window = {};
    GetWindowRect(dialog, &window);
    MONITORINFO monitor = {sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST), &monitor);
    const int needed_height = content_height + Gap(dialog) * static_cast<int>(state->lists.size() + 1);
    const int extra_width = std::max(0, std::min(content_width - static_cast<int>(top.right - top.left), static_cast<int>(monitor.rcWork.right - monitor.rcWork.left) * 9 / 10 - static_cast<int>(window.right - window.left)));
    const int extra_height = std::max(0, std::min(needed_height - static_cast<int>(bottom.top - top.bottom), static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top) * 85 / 100 - static_cast<int>(window.bottom - window.top)));
    // anchors are captured after WM_INITDIALOG, set opening positions at the grown size
    for (const int id : {kIdentifierId, kActionId, IDOK, IDCANCEL})
    {
        RECT rect = ControlRect(dialog, id);
        if (id == kIdentifierId)
        {
            rect.right += extra_width;
        }
        else
        {
            OffsetRect(&rect, id == kActionId ? 0 : extra_width, extra_height);
        }
        Place(GetDlgItem(dialog, id), rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
    }
    SetWindowPos(dialog, nullptr, 0, 0, window.right - window.left + extra_width, window.bottom - window.top + extra_height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        state = reinterpret_cast<State*>(lparam);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        const TablesSpec& spec = *state->spec;
        SetWindowTextW(dialog, std::wstring(spec.title).c_str());
        SetDlgItemTextW(dialog, kIdentifierId, std::wstring(spec.identifier).c_str());
        SetDlgItemTextW(dialog, kActionId, std::wstring(spec.action_label).c_str());
        SetDlgItemTextW(dialog, IDOK, Text(L"Copy"));
        SetDlgItemTextW(dialog, IDCANCEL, Text(L"Close"));
        ShowWindow(GetDlgItem(dialog, kActionId), spec.action_label.empty() ? SW_HIDE : SW_SHOW);
        EnableWindow(GetDlgItem(dialog, kActionId), spec.action != nullptr);
        SendMessageW(dialog, DM_SETDEFID, IDCANCEL, 0);
        FitDialogText(dialog);
        AnchorDialogControls(dialog, {
                                         {kIdentifierId, kAnchorLeft | kAnchorTop | kAnchorRight},
                                         {kActionId, kAnchorLeft | kAnchorBottom},
                                         {IDOK, kAnchorRight | kAnchorBottom},
                                         {IDCANCEL, kAnchorRight | kAnchorBottom},
                                     });
        RECT client = {};
        GetClientRect(dialog, &client);
        state->layout_dpi = DpiForWindow(dialog);
        state->right_margin_dip = Unscale(client.right - ControlRect(dialog, kIdentifierId).right, state->layout_dpi);
        state->bottom_offset_dip = Unscale(client.bottom - ControlRect(dialog, IDOK).top, state->layout_dpi);
        state->min_width_dip = Unscale(client.right, state->layout_dpi);
        state->min_height_dip = Unscale(client.bottom, state->layout_dpi);
        CreateLists(dialog, state);
        Layout(dialog, state);
        state->ready = true;
        if (!state->lists.empty())
        {
            SetFocus(state->lists.front());
        }
        return FALSE;
    }
    if (!state)
    {
        return FALSE;
    }
    if (message == WM_SIZE)
    {
        Layout(dialog, state);
        return FALSE;
    }
    if (message == WM_GETMINMAXINFO && state->min_width_dip > 0)
    {
        RaiseMinTrackSize(dialog, reinterpret_cast<MINMAXINFO*>(lparam), {state->min_width_dip, state->min_height_dip}, DpiForWindow(dialog), true);
        return TRUE;
    }
    switch (message)
    {
    case WM_NOTIFY:
        {
            const auto* header = reinterpret_cast<NMHDR*>(lparam);
            const auto list = std::find(state->lists.begin(), state->lists.end(), header->hwndFrom);
            const auto* change = reinterpret_cast<const NMLISTVIEW*>(lparam);
            const size_t index = static_cast<size_t>(list - state->lists.begin());
            if (list != state->lists.end() && header->code == LVN_COLUMNCLICK)
            {
                bool ascending = state->sort_ascending[index];
                SortListViewItems(*list, change->iSubItem, true, &state->sort_columns[index], &ascending, nullptr, CompareTextRows, const_cast<TextRows*>(&state->spec->tables[index].rows));
                state->sort_ascending[index] = ascending;
                return TRUE;
            }
            if (list != state->lists.end() && header->code == LVN_ITEMCHANGED && state->ready && HasChecks(*state, index) && ((change->uNewState ^ change->uOldState) & LVIS_STATEIMAGEMASK) &&
                change->iItem >= 0)
            {
                const bool checked = ListView_GetCheckState(*list, change->iItem) != FALSE;
                if (!state->spec->on_check(state->spec->context, dialog, static_cast<size_t>(change->lParam), checked))
                {
                    state->ready = false;
                    ListView_SetCheckState(*list, change->iItem, !checked);
                    state->ready = true;
                }
            }
            return FALSE;
        }
    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case kActionId:
            if (state->spec->action)
            {
                state->spec->action(state->spec->context, dialog);
            }
            return TRUE;
        case IDOK:
            CopyTextToClipboard(dialog, TablesText(*state->spec));
            return TRUE;
        case IDCANCEL:
            EndDialogWindow(dialog, IDCANCEL);
            return TRUE;
        default:
            return FALSE;
        }
    default:
        return FALSE;
    }
}

}

void ShowTables(HWND owner, const TablesSpec& spec)
{
    State state;
    state.spec = &spec;
    RunProcDialog(owner, L"WIN32UI_TABLES", DialogProc, reinterpret_cast<LPARAM>(&state));
}

}
