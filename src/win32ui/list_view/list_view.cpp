// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/list_view/list_view.h"

#include "win32ui/dialog/dialog_layout_internal.h"
#include "win32ui/dialog/message_dialog.h"
#include "win32ui/gdi/buffered_paint.h"
#include "win32ui/gdi/gdi_cache.h"
#include "win32ui/gdi/icon_loader.h"
#include "win32ui/list_view/column_model_internal.h"
#include "win32ui/list_view/list_view_internal.h"
#include "win32ui/menu/popup_menu.h"
#include "win32ui/theme/palette.h"
#include "win32ui/window_metrics.h"

#include <algorithm>
#include <climits>
#include <cwctype>
#include <iterator>
#include <memory>

#include <uxtheme.h>
#include <vsstyle.h>
#include <windowsx.h>

namespace win32ui
{

namespace
{

constexpr UINT_PTR kListSubclassId = 1;
constexpr UINT_PTR kParentSubclassId = 0x4C56;
constexpr int kHeaderTextPadding = 8;
constexpr int kDefaultWidthDip = 100;
constexpr int kMenuFitColumn = 1;
constexpr int kMenuFitAll = 2;
constexpr int kMenuReset = 3;
constexpr int kMenuColumnBase = 16;
constexpr int kMenuCopy = 1;
constexpr int kMenuSelectAll = 2;
constexpr size_t kMaxCellText = size_t{1} << 20;
constexpr DWORD kTypeTimeoutMs = 1000;
constexpr int kGridButtonWidth = 22;
constexpr int kGridGlyphSize = 16;
constexpr int kGridCommand = 1;
constexpr int kTipMaxWidth = 900;
constexpr int kTipCursorGap = 18;
// treat longer text as truncated without measuring it
constexpr size_t kTipMeasureLimit = 512;
constexpr size_t kTipTextLimit = 2000;
constexpr size_t kTipLineLimit = 60;
constexpr int kTipLabelInset = 8;
constexpr int kTipCellInset = 16;

struct ListState
{
    const ListViewHooks* hooks = nullptr;
    bool dialog_keys = false;
    std::vector<ListColumn> columns;
    std::vector<int> shown_dip;
    ColumnModel model;
    HTHEME header_theme = nullptr;
    std::wstring typed;
    DWORD typed_at = 0;
    HWND grid_button = nullptr;
    HIMAGELIST grid_images = nullptr;
    std::vector<int> grid_edges;
    bool grid_rows = false;
    HWND tip = nullptr;
    std::wstring tip_text;
    int tip_row = -1;
    int tip_column = -1;
    bool resize_posted = false;
    int paint_batches = 0;
    bool paint_skipped = false;
    LONG paint_left = 0;
    int paint_scroll = 0;
};

// one grid setting for every attached list
bool g_grid_lines = false;
std::vector<HWND> g_lists;

struct AttachedList
{
    HWND list;
    ListState* state;
};

// the lists this thread attached, GetWindowSubclass costs a GetProp kernel call on every custom draw notification
thread_local std::vector<AttachedList> t_attached;

struct SubclassedHeader
{
    HWND header;
    WNDPROC next;
    ListState* state;
};

// the native list asks its header for every columns rectangle in each hit test and paint, a SetWindowSubclass layer
// adds GetProp kernel calls to each, a header has no comctl32 chain so a plain window procedure keeps app subclass order
thread_local std::vector<SubclassedHeader> t_headers;

LRESULT CALLBACK ListProc(HWND list, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data);
LRESULT CALLBACK HeaderProc(HWND header, UINT message, WPARAM wparam, LPARAM lparam);
void HookParent(HWND list, const ListState* state);

SubclassedHeader* HeaderEntry(HWND header)
{
    for (SubclassedHeader& entry : t_headers)
    {
        if (entry.header == header)
        {
            return &entry;
        }
    }
    return nullptr;
}

ListState* StateOf(HWND list)
{
    for (const AttachedList& entry : t_attached)
    {
        if (entry.list == list)
        {
            return entry.state;
        }
    }
    // a list is attached on its own thread, only another thread's list can be missing here
    DWORD_PTR data = 0;
    return list && GetWindowThreadProcessId(list, nullptr) != GetCurrentThreadId() && GetWindowSubclass(list, ListProc, kListSubclassId, &data) ? reinterpret_cast<ListState*>(data) : nullptr;
}

// callbacks and notifications run app code that can destroy the list
bool Alive(HWND list, const ListState* state)
{
    return state ? StateOf(list) == state : IsWindow(list) != FALSE;
}

// column width change paints the columns right of it at once, a batch of changes skips those paints and invalidates
// once at its end, from the left edge of the leftmost changed column unless the list scrolled sideways
class PaintBatch
{
  public:
    // subitem -1 for changes to any column
    PaintBatch(HWND list, int subitem)
        : list_(list), state_(StateOf(list))
    {
        if (!state_)
        {
            return;
        }
        RECT column = {};
        const LONG left = subitem >= 0 && ListColumnRect(list, subitem, &column) ? column.left : LONG_MIN;
        if (state_->paint_batches++ == 0)
        {
            state_->paint_skipped = false;
            state_->paint_left = left;
            state_->paint_scroll = GetScrollPos(list, SB_HORZ);
        }
        state_->paint_left = std::min(state_->paint_left, left);
    }
    ~PaintBatch()
    {
        if (!state_ || !Alive(list_, state_) || --state_->paint_batches > 0 || !state_->paint_skipped)
        {
            return;
        }
        RECT area = {};
        GetClientRect(list_, &area);
        if (GetScrollPos(list_, SB_HORZ) == state_->paint_scroll)
        {
            area.left = std::max(area.left, state_->paint_left);
        }
        InvalidateRect(list_, &area, TRUE);
    }
    PaintBatch(const PaintBatch&) = delete;
    PaintBatch& operator=(const PaintBatch&) = delete;

  private:
    HWND list_;
    ListState* state_;
};

// list and header colors, read from the palette at paint time
struct ListColors
{
    // under headers and lists without a background override
    COLORREF panel = CLR_NONE;
    COLORREF text = CLR_NONE;
    // sort arrow when visual styles provide none
    COLORREF muted = CLR_NONE;
    COLORREF hover = CLR_NONE;
    COLORREF pressed = CLR_NONE;
    COLORREF border = CLR_NONE;
};

ListColors ColorsOf()
{
    const ThemeColors& colors = CurrentThemeColors();
    return ListColors{colors.panel, colors.text, colors.muted_text, colors.hover, colors.selection, colors.border};
}

void HideFocusRectangle(HWND list)
{
    if (!(SendMessageW(list, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS))
    {
        SendMessageW(list, WM_UPDATEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
    }
}

const wchar_t* LabelOf(const ListState* state, ListLabel label)
{
    const wchar_t* text = state && state->hooks && state->hooks->label ? state->hooks->label(label) : nullptr;
    return text ? text : L"";
}

void ColumnsChanged(HWND list, const ListState* state)
{
    if (state && state->hooks && state->hooks->columns_changed)
    {
        state->hooks->columns_changed(state->hooks->context, list);
    }
}

UINT ColumnsResizedMessage()
{
    static const UINT message = RegisterWindowMessageW(L"win32ui.list_view.columns_resized");
    return message;
}

// columns in subitem numbers, hidden ones included
int ColumnCount(HWND list)
{
    if (const ListState* state = StateOf(list))
    {
        return state->model.Count();
    }
    HWND header = ListView_GetHeader(list);
    return header ? Header_GetItemCount(header) : 0;
}

std::wstring ColumnTitle(HWND list, int subitem)
{
    if (const ListState* state = StateOf(list))
    {
        return state->model.Title(subitem);
    }
    wchar_t text[256] = {};
    HDITEMW item = {};
    item.mask = HDI_TEXT;
    item.pszText = text;
    item.cchTextMax = static_cast<int>(std::size(text));
    Header_GetItem(ListView_GetHeader(list), subitem, &item);
    return text;
}

// header positions to subitems, the same while no column is hidden
int HeaderSubitem(HWND list, int position)
{
    const ListState* state = StateOf(list);
    return state ? state->model.Subitem(position) : position;
}

void RememberWidths(HWND list, ListState* state)
{
    const int count = ColumnCount(list);
    state->shown_dip.resize(static_cast<size_t>(std::max(count, 0)), kDefaultWidthDip);
    const UINT dpi = WindowDpi(list);
    for (int column = 0; column < count; ++column)
    {
        const int width = ListView_GetColumnWidth(list, column);
        if (width > 0)
        {
            state->shown_dip[static_cast<size_t>(column)] = Unscale(width, dpi);
        }
    }
}

void ReleaseHeaderTheme(ListState* state)
{
    if (state && state->header_theme)
    {
        CloseThemeData(state->header_theme);
        state->header_theme = nullptr;
    }
}

// list background, falling back to the panel color
COLORREF SurfaceColor(HWND list, const ListColors& colors)
{
    const COLORREF background = ListView_GetBkColor(list);
    return background == CLR_NONE || background == CLR_DEFAULT ? colors.panel : background;
}

HBRUSH SurfaceBrush(HWND list, const ListColors& colors)
{
    return CachedBrush(SurfaceColor(list, colors));
}

void PaintHeader(HWND header, HDC hdc, ListState* state)
{
    const ListColors colors = ColorsOf();
    HBRUSH surface = SurfaceBrush(GetParent(header), colors);
    RECT client = {};
    GetClientRect(header, &client);
    FillRect(hdc, &client, surface);

    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(header, WM_GETFONT, 0, 0));
    HGDIOBJ old_font = font ? SelectObject(hdc, font) : nullptr;

    if (!state->header_theme)
    {
        state->header_theme = OpenThemeData(header, VSCLASS_HEADER);
    }
    SIZE arrow_size = {0, 0};
    const bool themed_arrow = state->header_theme && IsThemePartDefined(state->header_theme, HP_HEADERSORTARROW, 0);
    if (themed_arrow)
    {
        GetThemePartSize(state->header_theme, hdc, HP_HEADERSORTARROW, HSAS_SORTEDUP, nullptr, TS_TRUE, &arrow_size);
    }
    if (arrow_size.cx <= 0 || arrow_size.cy <= 0)
    {
        arrow_size = {8, 8};
    }

    POINT cursor = {};
    int hot_item = -1;
    if (GetCursorPos(&cursor) && WindowFromPoint(cursor) == header)
    {
        ScreenToClient(header, &cursor);
        HDHITTESTINFO hit = {};
        hit.pt = cursor;
        hot_item = static_cast<int>(SendMessageW(header, HDM_HITTEST, 0, reinterpret_cast<LPARAM>(&hit)));
    }
    const bool pressed = GetCapture() == header && GetKeyState(VK_LBUTTON) < 0;

    RECT clip = {};
    GetClipBox(hdc, &clip);
    const int count = Header_GetItemCount(header);
    for (int index = 0; index < count; ++index)
    {
        RECT rect = {};
        RECT visible = {};
        if (!Header_GetItemRect(header, index, &rect) || !IntersectRect(&visible, &rect, &clip))
        {
            continue;
        }
        wchar_t text[256] = {};
        HDITEMW item = {};
        item.mask = HDI_TEXT | HDI_FORMAT;
        item.pszText = text;
        item.cchTextMax = static_cast<int>(std::size(text));
        Header_GetItem(header, index, &item);
        const bool sorted_up = (item.fmt & HDF_SORTUP) != 0;
        const bool sorted_down = (item.fmt & HDF_SORTDOWN) != 0;

        FillRect(hdc, &rect, index == hot_item ? CachedBrush(pressed ? colors.pressed : colors.hover) : surface);
        RECT divider = {rect.right - 1, rect.top, rect.right, rect.bottom};
        FillRect(hdc, &divider, CachedBrush(colors.border));

        RECT text_rect = rect;
        text_rect.left += kHeaderTextPadding;
        text_rect.right -= kHeaderTextPadding;
        UINT format = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
        if (item.fmt & HDF_RIGHT)
        {
            format |= DT_RIGHT;
        }
        else if (item.fmt & HDF_CENTER)
        {
            format |= DT_CENTER;
        }
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, colors.text);
        DrawTextW(hdc, text, -1, &text_rect, format);

        if (themed_arrow && (sorted_up || sorted_down))
        {
            RECT arrow_rect = rect;
            arrow_rect.right -= 1;
            arrow_rect.bottom = rect.top + arrow_size.cy;
            DrawThemeBackground(state->header_theme, hdc, HP_HEADERSORTARROW, sorted_up ? HSAS_SORTEDUP : HSAS_SORTEDDOWN, &arrow_rect, nullptr);
        }
        else if (sorted_up || sorted_down)
        {
            // classic mode has no visual style to draw it
            const int mid = (rect.left + rect.right - 1) / 2;
            const int base = sorted_up ? rect.top + 4 : rect.top + 1;
            const int tip = sorted_up ? rect.top + 1 : rect.top + 4;
            const POINT arrow[3] = {{mid - 3, base}, {mid + 3, base}, {mid, tip}};
            HGDIOBJ old_pen = SelectObject(hdc, CachedPen(colors.muted));
            HGDIOBJ old_brush = SelectObject(hdc, CachedBrush(colors.muted));
            Polygon(hdc, arrow, 3);
            SelectObject(hdc, old_brush);
            SelectObject(hdc, old_pen);
        }
    }
    if (old_font)
    {
        SelectObject(hdc, old_font);
    }
}

void ShowColumnMenu(HWND list, ListState* state, POINT screen)
{
    HWND header = ListView_GetHeader(list);
    const int count = ColumnCount(list);
    if (count <= 0)
    {
        return;
    }
    RememberWidths(list, state);
    POINT client = screen;
    ScreenToClient(header, &client);
    HDHITTESTINFO hit = {};
    hit.pt = client;
    int column = HeaderSubitem(list, static_cast<int>(SendMessageW(header, HDM_HITTEST, 0, reinterpret_cast<LPARAM>(&hit))));
    // unavailable columns must not be fitted open through their dividers
    if (column >= 0 && static_cast<size_t>(column) < state->columns.size() && !state->columns[static_cast<size_t>(column)].available)
    {
        column = -1;
    }
    HMENU menu = CreatePopupMenu();
    if (!menu)
    {
        return;
    }
    AppendMenuW(menu, MF_STRING | (column >= 0 ? 0 : MF_GRAYED), kMenuFitColumn, LabelOf(state, ListLabel::fit_column));
    AppendMenuW(menu, MF_STRING, kMenuFitAll, LabelOf(state, ListLabel::fit_all));
    if (!state->columns.empty())
    {
        AppendMenuW(menu, MF_STRING, kMenuReset, LabelOf(state, ListLabel::reset_columns));
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (int index = 0; index < count; ++index)
    {
        if (static_cast<size_t>(index) < state->columns.size() && !state->columns[static_cast<size_t>(index)].available)
        {
            continue;
        }
        const UINT checked = ListView_GetColumnWidth(list, index) > 0 ? MF_CHECKED : 0;
        // the first column holds the item text and state
        const UINT fixed = index == 0 ? MF_GRAYED : 0;
        AppendMenuW(menu, MF_STRING | checked | fixed, static_cast<UINT_PTR>(kMenuColumnBase + index), MenuName(ColumnTitle(list, index)).c_str());
    }
    const int chosen = TrackContextMenu(menu, screen, list);
    // menu loop dispatched messages, list may be gone or columns changed
    if (StateOf(list) != state || ColumnCount(list) != count)
    {
        return;
    }
    if (chosen == kMenuFitColumn && column >= 0)
    {
        FitListColumn(list, column);
    }
    else if (chosen == kMenuFitAll)
    {
        for (int index = 0; index < count; ++index)
        {
            if (ListView_GetColumnWidth(list, index) > 0)
            {
                FitListColumn(list, index);
            }
        }
    }
    else if (chosen == kMenuReset)
    {
        ResetListColumns(list);
    }
    else if (chosen > kMenuColumnBase && chosen < kMenuColumnBase + count)
    {
        const int index = chosen - kMenuColumnBase;
        const bool shown = ListView_GetColumnWidth(list, index) > 0;
        ListView_SetColumnWidth(list, index, shown ? 0 : std::max(1, Scale(state->shown_dip[static_cast<size_t>(index)], WindowDpi(list))));
    }
    else
    {
        return;
    }
    RememberWidths(list, state);
    ColumnsChanged(list, state);
}

// keep the largest buffer so consecutive long cells need one message each, shown columns of an attached list are
// read below its subclasses, a hidden column through the column model
void ReadCell(HWND list, const ListState* state, int row, int subitem, std::vector<wchar_t>* buffer, std::wstring* text)
{
    if (state && state->hooks && state->hooks->cell_text)
    {
        if (state->hooks->cell_text(state->hooks->context, list, row, subitem, text))
        {
            return;
        }
        if (!Alive(list, state))
        {
            text->clear();
            return;
        }
    }
    // another thread reads through SendMessage, in subitem numbers
    const int position = state && state->model.Local() ? state->model.Position(subitem) : -1;
    // returned length only counts text that fits, grow the buffer while full
    buffer->resize(std::max<size_t>(buffer->size(), 256));
    LVITEMW item = {};
    item.iSubItem = position >= 0 ? position : subitem;
    for (;;)
    {
        item.pszText = buffer->data();
        item.cchTextMax = static_cast<int>(buffer->size());
        const LPARAM pointer = reinterpret_cast<LPARAM>(&item);
        const LRESULT sent = position >= 0 ? state->model.Direct(list, LVM_GETITEMTEXTW, static_cast<WPARAM>(row), pointer) : SendMessageW(list, LVM_GETITEMTEXTW, static_cast<WPARAM>(row), pointer);
        const size_t length = static_cast<size_t>(std::max<LRESULT>(0, sent));
        if (length + 1 < buffer->size() || buffer->size() >= kMaxCellText)
        {
            text->assign(buffer->data(), std::min(length, buffer->size()));
            return;
        }
        buffer->resize(buffer->size() * 2);
    }
}

// widest cell text across all rows and visible rows, in list font pixels
std::pair<int, int> CellTextWidths(HWND list, int column)
{
    const ListState* state = StateOf(list);
    const int count = ListView_GetItemCount(list);
    const int top = ListView_GetTopIndex(list);
    const int last = top + ListView_GetCountPerPage(list);
    if (state)
    {
        HookParent(list, state);
    }
    HDC dc = GetDC(list);
    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(list, WM_GETFONT, 0, 0));
    const HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    std::vector<wchar_t> buffer;
    std::wstring text;
    int all = 0;
    int shown = 0;
    for (int row = 0; row < count; ++row)
    {
        ReadCell(list, state, row, column, &buffer, &text);
        if (!Alive(list, state))
        {
            break;
        }
        SIZE size = {};
        GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
        all = std::max(all, static_cast<int>(size.cx));
        if (row >= top && row <= last)
        {
            shown = std::max(shown, static_cast<int>(size.cx));
        }
    }
    if (old)
    {
        SelectObject(dc, old);
    }
    ReleaseDC(list, dc);
    return {all, shown};
}

bool TypedTextAlive(const ListState* state)
{
    return !state->typed.empty() && GetTickCount() - state->typed_at <= kTypeTimeoutMs;
}

bool StartsWith(const std::wstring& text, const std::wstring& prefix)
{
    return text.size() >= prefix.size() && CompareStringOrdinal(text.data(), static_cast<int>(prefix.size()), prefix.data(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
}

int CompareText(const std::wstring& left, const std::wstring& right)
{
    return CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(), static_cast<int>(right.size()), TRUE) - CSTR_EQUAL;
}

// false for non search characters, spaces only extend existing typed text
bool TypeToSelect(HWND list, ListState* state, wchar_t character)
{
    if (character != L'\b' && (!std::iswprint(character) || character == L'\t'))
    {
        return false;
    }
    if (!TypedTextAlive(state))
    {
        state->typed.clear();
    }
    if (character == L' ' && state->typed.empty())
    {
        return true;
    }
    state->typed_at = GetTickCount();
    if (character != L'\b')
    {
        state->typed.push_back(character);
    }
    else if (!state->typed.empty())
    {
        state->typed.pop_back();
    }
    const int count = ListView_GetItemCount(list);
    if (state->typed.empty() || count <= 0)
    {
        return true;
    }
    // first prefix match in shown order, else first later row in text order
    int match = -1;
    int nearest = -1;
    std::vector<wchar_t> buffer;
    std::wstring text;
    std::wstring nearest_text;
    HookParent(list, state);
    for (int row = 0; row < count && match < 0; ++row)
    {
        ReadCell(list, state, row, 0, &buffer, &text);
        if (!Alive(list, state))
        {
            return true;
        }
        if (StartsWith(text, state->typed))
        {
            match = row;
        }
        else if (CompareText(text, state->typed) > 0 && (nearest < 0 || CompareText(text, nearest_text) < 0))
        {
            nearest = row;
            nearest_text = text;
        }
    }
    const int row = match >= 0 ? match : nearest >= 0 ? nearest
                                                      : count - 1;
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetSelectionMark(list, row);
    ListView_EnsureVisible(list, row, FALSE);
    return true;
}

void CopySelection(HWND list)
{
    const std::wstring text = SelectedRowsText(list);
    if (text.empty())
    {
        return;
    }
    CopyTextToClipboard(GetAncestor(list, GA_ROOT), text);
}

void ShowRowMenu(HWND list, ListState* state, LPARAM lparam)
{
    POINT screen = {};
    const int row = ContextMenuRow(list, lparam, &screen);
    if (row >= 0 && !ListView_GetItemState(list, row, LVIS_SELECTED))
    {
        ListView_SetItemState(list, -1, 0, LVIS_SELECTED);
        ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        // selection notification may close the list
        if (StateOf(list) != state)
        {
            return;
        }
    }
    if (state->hooks && state->hooks->row_menu && state->hooks->row_menu(state->hooks->context, list, row, screen))
    {
        return;
    }
    const bool selected = ListView_GetSelectedCount(list) > 0;
    const bool can_select_all = ListView_GetItemCount(list) > 0 && !(GetWindowLongPtrW(list, GWL_STYLE) & LVS_SINGLESEL);
    HMENU menu = CreatePopupMenu();
    if (!menu)
    {
        return;
    }
    AppendMenuItem(menu, MF_STRING | (selected ? 0 : MF_GRAYED), kMenuCopy, LabelOf(state, ListLabel::copy), L"Ctrl+C");
    AppendMenuItem(menu, MF_STRING | (can_select_all ? 0 : MF_GRAYED), kMenuSelectAll, LabelOf(state, ListLabel::select_all), L"Ctrl+A");
    const int chosen = TrackContextMenu(menu, screen, list);
    if (StateOf(list) != state)
    {
        return;
    }
    if (chosen == kMenuCopy)
    {
        CopySelection(list);
    }
    else if (chosen == kMenuSelectAll)
    {
        SelectAllListItems(list);
    }
}

void LoadGridIcon(HWND list, ListState* state)
{
    const UINT dpi = WindowDpi(list);
    const int size = Scale(kGridGlyphSize, dpi);
    HIMAGELIST images = ImageList_Create(size, size, ILC_COLOR32, 1, 1);
    if (!images)
    {
        return;
    }
    ImageList_SetBkColor(images, CLR_NONE);
    HICON icon = state->hooks && state->hooks->grid_icon ? state->hooks->grid_icon(dpi) : nullptr;
    ImageListAddOrBlank(images, icon, size);
    if (icon)
    {
        DestroyIcon(icon);
    }
    SendMessageW(state->grid_button, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(images));
    if (state->grid_images)
    {
        ImageList_Destroy(state->grid_images);
    }
    state->grid_images = images;
}

// right end of header, stays fixed during horizontal scrolling
void LayoutGridButton(HWND list, const ListState* state)
{
    HWND button = state->grid_button;
    HWND header = ListView_GetHeader(list);
    RECT client = {};
    RECT header_client = {};
    const bool shown = (GetWindowLongPtrW(list, GWL_STYLE) & (WS_VISIBLE | LVS_NOCOLUMNHEADER)) == WS_VISIBLE;
    if (!button || !header || !shown || !GetClientRect(list, &client) || !GetClientRect(header, &header_client))
    {
        if (button)
        {
            ShowWindow(button, SW_HIDE);
        }
        return;
    }
    MapWindowPoints(list, GetParent(button), reinterpret_cast<POINT*>(&client), 2);
    const int width = std::min<int>(client.right - client.left, Scale(kGridButtonWidth, WindowDpi(list)));
    const int height = header_client.bottom;
    if (width <= 0 || height <= 0)
    {
        ShowWindow(button, SW_HIDE);
        return;
    }
    SendMessageW(button, TB_SETBUTTONSIZE, 0, MAKELPARAM(width, height));
    // place just above the list so siblings above it also cover the button
    HWND above = GetWindow(list, GW_HWNDPREV);
    const UINT order = above == button ? SWP_NOZORDER : 0;
    SetWindowPos(button, above && above != button ? above : HWND_TOP, client.right - width, client.top, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW | order);
}

void ApplyGridLines(bool shown)
{
    g_grid_lines = shown;
    for (HWND list : g_lists)
    {
        if (const ListState* state = StateOf(list); state && state->grid_button)
        {
            SendMessageW(state->grid_button, TB_CHECKBUTTON, kGridCommand, MAKELPARAM(shown ? TRUE : FALSE, 0));
        }
        InvalidateRect(list, nullptr, TRUE);
    }
}

// icon alone on the list background
LRESULT PaintGridButton(HWND list, const ListState* state, NMTBCUSTOMDRAW* draw)
{
    const ListColors colors = ColorsOf();
    switch (draw->nmcd.dwDrawStage)
    {
    case CDDS_PREPAINT:
        FillRect(draw->nmcd.hdc, &draw->nmcd.rc, SurfaceBrush(list, colors));
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
        {
            int width = 0;
            int height = 0;
            if (state->grid_images && ImageList_GetIconSize(state->grid_images, &width, &height))
            {
                const RECT& rect = draw->nmcd.rc;
                ImageList_Draw(state->grid_images, 0, draw->nmcd.hdc, rect.left + (rect.right - rect.left - width) / 2, rect.top + (rect.bottom - rect.top - height) / 2, ILD_TRANSPARENT);
            }
            return CDRF_SKIPDEFAULT;
        }
    default:
        return CDRF_DODEFAULT;
    }
}

void ReadGridEdges(HWND list, ListState* state)
{
    state->grid_edges.clear();
    HWND header = ListView_GetHeader(list);
    RECT header_rect = {};
    if (!header || !GetWindowRect(header, &header_rect))
    {
        return;
    }
    MapWindowPoints(nullptr, list, reinterpret_cast<POINT*>(&header_rect), 2);
    const int count = Header_GetItemCount(header);
    for (int index = 0; index < count; ++index)
    {
        RECT item = {};
        if (Header_GetItemRect(header, index, &item) && item.right > item.left)
        {
            state->grid_edges.push_back(header_rect.left + item.right - 1);
        }
    }
}

void PaintGrid(HWND list, const ListState* state, HDC hdc, const RECT& area, int first_line_y, int row_height)
{
    if (area.top >= area.bottom)
    {
        return;
    }
    HBRUSH brush = CachedBrush(ColorsOf().border);
    RECT client = {};
    GetClientRect(list, &client);
    for (const int x : state->grid_edges)
    {
        if (x >= client.left && x < client.right)
        {
            const RECT line = {x, area.top, x + 1, area.bottom};
            FillRect(hdc, &line, brush);
        }
    }
    for (int y = first_line_y; row_height > 0 && y < area.bottom; y += row_height)
    {
        if (y >= area.top)
        {
            const RECT line = {client.left, y, client.right, y + 1};
            FillRect(hdc, &line, brush);
        }
    }
}

// the client area below the header
bool BelowHeader(HWND list, RECT* area)
{
    HWND header = ListView_GetHeader(list);
    RECT header_rect = {};
    if (!header || !GetClientRect(list, area) || !GetWindowRect(header, &header_rect))
    {
        return false;
    }
    MapWindowPoints(nullptr, list, reinterpret_cast<POINT*>(&header_rect), 2);
    area->top = std::max<LONG>(area->top, header_rect.bottom);
    return true;
}

bool ItemBounds(HWND list, const ListState* state, int row, RECT* rect)
{
    *rect = {LVIR_BOUNDS};
    return state->model.Direct(list, LVM_GETITEMRECT, static_cast<WPARAM>(row), reinterpret_cast<LPARAM>(rect)) != FALSE;
}

// report rows are evenly spaced, the whole grid goes on once per paint from the top shown row down at its pitch
void PaintGridOnce(HWND list, const ListState* state, HDC hdc)
{
    RECT area = {};
    RECT row = {};
    if (!BelowHeader(list, &area))
    {
        return;
    }
    if (state->model.Direct(list, LVM_GETITEMCOUNT, 0, 0) > 0 && ItemBounds(list, state, static_cast<int>(state->model.Direct(list, LVM_GETTOPINDEX, 0, 0)), &row))
    {
        area.top = row.top;
        PaintGrid(list, state, hdc, area, row.bottom - 1, row.bottom - row.top);
        return;
    }
    PaintGrid(list, state, hdc, area, area.top, 0);
}

// group view rows draw their own lines, below the last row the grid goes on at its pitch
void PaintGridTail(HWND list, const ListState* state, HDC hdc)
{
    RECT area = {};
    RECT last = {};
    if (!BelowHeader(list, &area))
    {
        return;
    }
    const int count = static_cast<int>(state->model.Direct(list, LVM_GETITEMCOUNT, 0, 0));
    int row_height = 0;
    if (count > 0 && ItemBounds(list, state, count - 1, &last))
    {
        row_height = last.bottom - last.top;
        area.top = std::max(area.top, last.bottom);
    }
    PaintGrid(list, state, hdc, area, area.top + row_height - 1, row_height);
}

void AddCellTip(HWND list, ListState* state)
{
    state->tip = CreateTooltip(list);
    if (!state->tip)
    {
        return;
    }
    // ListProc relays the mouse, TTF_SUBCLASS would add a subclass layer to every message of the list
    TOOLINFOW info = {};
    info.cbSize = sizeof(info);
    info.uFlags = TTF_IDISHWND;
    info.hwnd = list;
    info.uId = reinterpret_cast<UINT_PTR>(list);
    info.lpszText = LPSTR_TEXTCALLBACKW;
    SendMessageW(state->tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    SendMessageW(state->tip, TTM_SETMAXTIPWIDTH, 0, Scale(kTipMaxWidth, WindowDpi(list)));
    ApplyFrame(state->hooks, state->tip);
}

// the messages TTF_SUBCLASS relays, all client mouse messages, WM_NCMOUSEMOVE and WM_MOUSELEAVE
void RelayToTip(HWND list, HWND tip, UINT message, WPARAM wparam, LPARAM lparam)
{
    if ((message < WM_MOUSEFIRST || message > WM_MOUSELAST) && message != WM_NCMOUSEMOVE && message != WM_MOUSELEAVE)
    {
        return;
    }
    const DWORD position = GetMessagePos();
    MSG relayed = {list, message, wparam, lparam, static_cast<DWORD>(GetMessageTime()), {GET_X_LPARAM(position), GET_Y_LPARAM(position)}};
    SendMessageW(tip, TTM_RELAYEVENT, static_cast<WPARAM>(GetMessageExtraInfo()), reinterpret_cast<LPARAM>(&relayed));
}

// full hovered cell text when truncated, otherwise empty
std::wstring CellTipText(HWND list, const ListState* state)
{
    const int row = state->tip_row;
    const int column = state->tip_column;
    RECT cell = {};
    if (row < 0 || column < 0 || !(column == 0 ? ListView_GetItemRect(list, row, &cell, LVIR_LABEL) : ListView_GetSubItemRect(list, row, column, LVIR_BOUNDS, &cell)))
    {
        return {};
    }
    std::vector<wchar_t> buffer;
    std::wstring text;
    HookParent(list, state);
    ReadCell(list, state, row, column, &buffer, &text);
    if (!Alive(list, state))
    {
        return {};
    }
    const int available = static_cast<int>(cell.right - cell.left) - Scale(column == 0 ? kTipLabelInset : kTipCellInset, WindowDpi(list));
    if (text.empty() || available <= 0 || (text.size() <= kTipMeasureLimit && ListView_GetStringWidth(list, text.c_str()) <= available))
    {
        return {};
    }
    // cap tooltip size for huge values
    size_t limit = std::min(text.size(), kTipTextLimit);
    for (size_t index = 0, lines = 0; index < limit; ++index)
    {
        if (text[index] == L'\n' && ++lines == kTipLineLimit)
        {
            limit = index;
        }
    }
    if (limit > 0 && limit < text.size() && IS_HIGH_SURROGATE(text[limit - 1]))
    {
        --limit;
    }
    if (limit < text.size())
    {
        text.resize(limit);
        text += LabelOf(state, ListLabel::more_text);
    }
    return text;
}

// beside the cursor, flipped to its other side at the edge of the work area
void PlaceCellTip(HWND list, const ListState* state)
{
    RECT tip = {};
    POINT cursor = {};
    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    if (!GetWindowRect(state->tip, &tip) || !GetCursorPos(&cursor) || !GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor))
    {
        return;
    }
    const int gap = Scale(kTipCursorGap, WindowDpi(list));
    const int width = tip.right - tip.left;
    const int height = tip.bottom - tip.top;
    int x = cursor.x + gap;
    int y = cursor.y + gap;
    if (x + width > monitor.rcWork.right)
    {
        x = std::max<int>(monitor.rcWork.left, cursor.x - gap - width);
    }
    if (y + height > monitor.rcWork.bottom)
    {
        y = std::max<int>(monitor.rcWork.top, cursor.y - gap - height);
    }
    SetWindowPos(state->tip, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

LRESULT CALLBACK HeaderProc(HWND header, UINT message, WPARAM wparam, LPARAM lparam)
{
    const SubclassedHeader* entry = HeaderEntry(header);
    if (!entry)
    {
        return DefWindowProcW(header, message, wparam, lparam);
    }
    // copies, a menu loop below can change the registry
    const WNDPROC next = entry->next;
    ListState* state = entry->state;
    switch (message)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintBuffered(header);
        return 0;
    case WM_PRINTCLIENT:
        PaintHeader(header, reinterpret_cast<HDC>(wparam), state);
        return 0;
    case WM_CONTEXTMENU:
        {
            POINT screen = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (screen.x == -1 && screen.y == -1)
            {
                RECT rect = {};
                GetWindowRect(header, &rect);
                screen = {rect.left + 12, rect.bottom - 4};
            }
            ShowColumnMenu(GetParent(header), state, screen);
            return 0;
        }
    case WM_THEMECHANGED:
        ReleaseHeaderTheme(state);
        InvalidateRect(header, nullptr, TRUE);
        break;
    case WM_SIZE:
        // font or DPI changes can change header height
        LayoutGridButton(GetParent(header), state);
        break;
    case WM_NCDESTROY:
        ReleaseHeaderTheme(state);
        std::erase_if(t_headers, [&](const SubclassedHeader& item) { return item.header == header; });
        break;
    default:
        break;
    }
    return CallWindowProcW(next, header, message, wparam, lparam);
}

// grid edges are cached per paint, horizontal scrolling repaints everything
LRESULT ScrollKeepingGrid(HWND list, UINT message, WPARAM wparam, LPARAM lparam)
{
    const int before = GetScrollPos(list, SB_HORZ);
    const LRESULT result = DefSubclassProc(list, message, wparam, lparam);
    if (GetScrollPos(list, SB_HORZ) != before)
    {
        InvalidateRect(list, nullptr, TRUE);
    }
    return result;
}

// list notifications carry native positions, the parent hears subitem numbers
LRESULT CALLBACK ParentProc(HWND parent, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR)
{
    if (message == WM_NOTIFY && lparam)
    {
        auto* header = reinterpret_cast<NMHDR*>(lparam);
        ListState* state = StateOf(header->hwndFrom);
        if (state && state->model.AnyHidden() && !state->model.IsRawNotify(header))
        {
            if (int* field = state->model.NotifySubitem(header))
            {
                const int position = *field;
                *field = state->model.Subitem(position);
                const LRESULT result = DefSubclassProc(parent, message, wparam, lparam);
                *field = position;
                return result;
            }
        }
    }
    else if (message == WM_NCDESTROY)
    {
        RemoveWindowSubclass(parent, ParentProc, kParentSubclassId);
    }
    return DefSubclassProc(parent, message, wparam, lparam);
}

// a list reparented with hidden columns gets it at its next paint
void HookParent(HWND list, const ListState* state)
{
    HWND parent = GetParent(list);
    if (state->model.AnyHidden() && parent && !GetWindowSubclass(parent, ParentProc, kParentSubclassId, nullptr))
    {
        SetWindowSubclass(parent, ParentProc, kParentSubclassId, 0);
    }
}

LRESULT CALLBACK ListProc(HWND list, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data)
{
    auto* state = reinterpret_cast<ListState*>(data);
    if (state->model.TakeNative(message, wparam, lparam))
    {
        return DefSubclassProc(list, message, wparam, lparam);
    }
    if (LRESULT translated = 0; state->model.Translate(list, message, wparam, lparam, &translated))
    {
        // a translated message can notify the parent, which can destroy the list
        if (Alive(list, state))
        {
            HookParent(list, state);
        }
        return translated;
    }
    if (state->resize_posted && message == ColumnsResizedMessage())
    {
        state->resize_posted = false;
        if (static_cast<size_t>(state->model.Count()) == state->shown_dip.size() && !state->shown_dip.empty())
        {
            state->model.Reconcile(list, Scale(state->shown_dip[0], WindowDpi(list)));
        }
        else
        {
            state->model.Reconcile(list, Scale(kDefaultWidthDip, WindowDpi(list)));
        }
        HookParent(list, state);
        RememberWidths(list, state);
        ColumnsChanged(list, state);
        return 0;
    }
    if (message == WM_PAINT)
    {
        // the region stays invalid, the batch invalidates what it moved and the next paint covers both
        if (state->paint_batches > 0)
        {
            state->paint_skipped = true;
            return 0;
        }
        HookParent(list, state);
    }
    if (state->tip)
    {
        RelayToTip(list, state->tip, message, wparam, lparam);
    }
    switch (message)
    {
    case WM_COMMAND:
        if (state->grid_button && reinterpret_cast<HWND>(lparam) == state->grid_button && LOWORD(wparam) == kGridCommand)
        {
            // check button already toggled before notifying the list
            const bool shown = SendMessageW(state->grid_button, TB_ISBUTTONCHECKED, kGridCommand, 0) != 0;
            ApplyGridLines(shown);
            if (state->hooks && state->hooks->grid_changed)
            {
                state->hooks->grid_changed(shown);
            }
            return 0;
        }
        break;
    case WM_NOTIFY:
        {
            const auto* header = reinterpret_cast<const NMHDR*>(lparam);
            if (state->grid_button && header->hwndFrom == state->grid_button && header->code == NM_CUSTOMDRAW)
            {
                return PaintGridButton(list, state, reinterpret_cast<NMTBCUSTOMDRAW*>(lparam));
            }
            if (state->tip && header->hwndFrom == state->tip && header->code == TTN_GETDISPINFOW)
            {
                std::wstring text = CellTipText(list, state);
                if (!Alive(list, state))
                {
                    return 0;
                }
                state->tip_text = std::move(text);
                reinterpret_cast<NMTTDISPINFOW*>(lparam)->lpszText = state->tip_text.data();
                return 0;
            }
            if (state->tip && header->hwndFrom == state->tip && header->code == TTN_SHOW)
            {
                PlaceCellTip(list, state);
                return TRUE;
            }
            if (header->hwndFrom != ListView_GetHeader(list))
            {
                break;
            }
            // column width or order changes move grid lines in rows the list doesnt repaint
            if (g_grid_lines && header->code == HDN_ITEMCHANGEDW)
            {
                InvalidateRect(list, nullptr, FALSE);
            }
            if (header->code == HDN_BEGINTRACKW || header->code == HDN_DIVIDERDBLCLICKW)
            {
                const int column = state->model.Subitem(reinterpret_cast<const NMHEADERW*>(lparam)->iItem);
                // dragging an unavailable column divider would reveal the column
                if (column >= 0 && static_cast<size_t>(column) < state->columns.size() && !state->columns[static_cast<size_t>(column)].available)
                {
                    return TRUE;
                }
                if (header->code == HDN_DIVIDERDBLCLICKW)
                {
                    // replace native fitting, which measures only visible rows in owner data lists
                    FitListColumn(list, column);
                }
            }
            // header applies width after HDN_ENDTRACK
            if ((header->code == HDN_ENDTRACKW || header->code == HDN_DIVIDERDBLCLICKW) && !state->resize_posted)
            {
                state->resize_posted = PostMessageW(list, ColumnsResizedMessage(), 0, 0) != FALSE;
            }
            if (header->code == HDN_DIVIDERDBLCLICKW)
            {
                return 0;
            }
            break;
        }
    case LVM_SETITEMCOUNT:
    case LVM_DELETEALLITEMS:
    case LVM_DELETEITEM:
        if (state->tip)
        {
            SendMessageW(state->tip, TTM_POP, 0, 0);
        }
        break;
    case WM_MOUSEMOVE:
        if (state->tip)
        {
            LVHITTESTINFO hit = {};
            hit.pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            const int row = static_cast<int>(state->model.Direct(list, LVM_SUBITEMHITTEST, 0, reinterpret_cast<LPARAM>(&hit)));
            const int column = row >= 0 ? state->model.Subitem(hit.iSubItem) : -1;
            if (row != state->tip_row || column != state->tip_column)
            {
                // restart tooltip delay for each cell
                state->tip_row = row;
                state->tip_column = column;
                SendMessageW(state->tip, TTM_POP, 0, 0);
            }
        }
        break;
    case WM_HSCROLL:
    case WM_VSCROLL:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case LVM_SCROLL:
    case LVM_ENSUREVISIBLE:
        if (state->tip)
        {
            SendMessageW(state->tip, TTM_POP, 0, 0);
        }
        if (g_grid_lines)
        {
            return ScrollKeepingGrid(list, message, wparam, lparam);
        }
        break;
    case WM_WINDOWPOSCHANGED:
        if (state->grid_button)
        {
            const LRESULT result = DefSubclassProc(list, message, wparam, lparam);
            LayoutGridButton(list, state);
            return result;
        }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        HideFocusRectangle(list);
        break;
    case WM_UPDATEUISTATE:
        {
            const LRESULT result = DefSubclassProc(list, message, wparam, lparam);
            // the lists own UIS_SET comes back here, while Windows keeps keyboard cues shown asking again would never end
            if (LOWORD(wparam) != UIS_SET)
            {
                HideFocusRectangle(list);
            }
            return result;
        }
    case WM_KEYDOWN:
        if (wparam == VK_SPACE && TypedTextAlive(state) && GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_MENU) >= 0)
        {
            return 0;
        }
        if (state->dialog_keys && GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_SHIFT) >= 0 && GetKeyState(VK_MENU) >= 0 && (wparam == 'C' || wparam == 'A'))
        {
            if (wparam == 'C')
            {
                CopySelection(list);
            }
            else
            {
                SelectAllListItems(list);
            }
            return 0;
        }
        if (g_grid_lines)
        {
            return ScrollKeepingGrid(list, message, wparam, lparam);
        }
        break;
    case WM_CHAR:
        // consume Ctrl+C and Ctrl+A control characters so the list doesnt search for them
        if (state->dialog_keys && (wparam == 0x03 || wparam == 0x01))
        {
            return 0;
        }
        // replace native search, owner data lists would send LVN_ODFINDITEM to the parent
        if (TypeToSelect(list, state, static_cast<wchar_t>(wparam)))
        {
            return 0;
        }
        break;
    case WM_RBUTTONDOWN:
        if (state->dialog_keys)
        {
            LVHITTESTINFO hit = {};
            hit.pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (ListView_HitTest(list, &hit) < 0)
            {
                SetFocus(list);
                return 0;
            }
        }
        break;
    case WM_CONTEXTMENU:
        if (state->dialog_keys && reinterpret_cast<HWND>(wparam) == list)
        {
            ShowRowMenu(list, state, lparam);
            return 0;
        }
        break;
    case WM_NCDESTROY:
        // sibling button and top level tooltip would otherwise outlive the list
        if (state->grid_button && IsWindow(state->grid_button))
        {
            DestroyWindow(state->grid_button);
        }
        if (state->grid_images)
        {
            ImageList_Destroy(state->grid_images);
        }
        if (state->tip && IsWindow(state->tip))
        {
            DestroyWindow(state->tip);
        }
        std::erase(g_lists, list);
        std::erase_if(t_attached, [&](const AttachedList& entry) { return entry.list == list; });
        RemoveWindowSubclass(list, ListProc, kListSubclassId);
        delete state;
        break;
    default:
        break;
    }
    return DefSubclassProc(list, message, wparam, lparam);
}

bool Attach(HWND list, const ListViewHooks* hooks, DWORD extra_styles, bool dialog_keys)
{
    if (!list)
    {
        return false;
    }
    const DWORD mask = LVS_EX_INFOTIP | LVS_EX_LABELTIP | LVS_EX_GRIDLINES | LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_BORDERSELECT | LVS_EX_TRACKSELECT |
                       LVS_EX_ONECLICKACTIVATE | LVS_EX_TWOCLICKACTIVATE | LVS_EX_UNDERLINEHOT;
    // cell tooltip replaces native label tips, shared grid setting controls grid drawing
    extra_styles &= ~static_cast<DWORD>(LVS_EX_LABELTIP | LVS_EX_INFOTIP | LVS_EX_GRIDLINES);
    ListView_SetExtendedListViewStyleEx(list, mask | extra_styles, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | extra_styles);
    if (hooks && hooks->frame)
    {
        AttachThemedBorder(list);
    }
    ListState* state = StateOf(list);
    if (!state)
    {
        auto created = std::make_unique<ListState>();
        if (!SetWindowSubclass(list, ListProc, kListSubclassId, reinterpret_cast<DWORD_PTR>(created.get())))
        {
            return false;
        }
        state = created.release();
        g_lists.push_back(list);
        t_attached.push_back({list, state});
    }
    state->hooks = hooks;
    state->dialog_keys = state->dialog_keys || dialog_keys;
    state->model.Load(list);
    HWND header = ListView_GetHeader(list);
    if (header && !HeaderEntry(header))
    {
        t_headers.push_back({header, reinterpret_cast<WNDPROC>(GetWindowLongPtrW(header, GWLP_WNDPROC)), state});
        if (!SetWindowLongPtrW(header, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HeaderProc)))
        {
            t_headers.pop_back();
        }
    }
    HideFocusRectangle(list);
    RememberWidths(list, state);
    if (!state->tip)
    {
        AddCellTip(list, state);
    }
    return true;
}

} // namespace

int ColumnSubItem(HWND list, int position)
{
    return HeaderSubitem(list, position);
}

void RecordListSort(HWND list, int subitem, int bits)
{
    if (ListState* state = StateOf(list))
    {
        state->model.RecordSort(subitem, bits);
    }
}

bool AttachListView(HWND list, const ListViewHooks* hooks, DWORD extra_styles)
{
    return Attach(list, hooks, extra_styles, false);
}

bool AttachDialogListView(HWND list, const ListViewHooks* hooks, DWORD extra_styles)
{
    return Attach(list, hooks, extra_styles, true);
}

void RefreshListView(HWND list)
{
    ListState* state = StateOf(list);
    ReleaseHeaderTheme(state);
    if (state)
    {
        HookParent(list, state);
    }
    if (HWND header = list ? ListView_GetHeader(list) : nullptr)
    {
        InvalidateRect(header, nullptr, TRUE);
    }
    if (state && state->grid_button)
    {
        ApplyFrame(state->hooks, state->grid_button);
        LoadGridIcon(list, state);
        LayoutGridButton(list, state);
        InvalidateRect(state->grid_button, nullptr, TRUE);
    }
    if (state && state->tip)
    {
        ApplyFrame(state->hooks, state->tip);
        SendMessageW(state->tip, TTM_SETMAXTIPWIDTH, 0, Scale(kTipMaxWidth, WindowDpi(list)));
    }
    InvalidateRect(list, nullptr, TRUE);
}

void ShowListGridButton(HWND list)
{
    ListState* state = StateOf(list);
    if (!state || state->grid_button || !GetParent(list))
    {
        return;
    }
    // TB_ADDSTRING takes a list of strings that ends with an empty one
    std::wstring label = LabelOf(state, ListLabel::grid_lines);
    HWND button = CreateWindowExW(0, TOOLBARCLASSNAMEW, label.c_str(), WS_CHILD | WS_CLIPSIBLINGS | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER | CCS_NOPARENTALIGN | CCS_NORESIZE, 0, 0, 0, 0, GetParent(list), nullptr, nullptr, nullptr);
    if (!button)
    {
        return;
    }
    label.push_back(L'\0');
    SendMessageW(button, TB_SETPARENT, reinterpret_cast<WPARAM>(list), 0);
    SendMessageW(button, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    // keep label as accessible name and tooltip without drawing it
    SendMessageW(button, TB_SETMAXTEXTROWS, 0, 0);
    SendMessageW(button, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DOUBLEBUFFER);
    TBBUTTON item = {};
    item.idCommand = kGridCommand;
    item.fsState = static_cast<BYTE>(TBSTATE_ENABLED | (g_grid_lines ? TBSTATE_CHECKED : 0));
    item.fsStyle = BTNS_CHECK;
    item.iString = SendMessageW(button, TB_ADDSTRINGW, 0, reinterpret_cast<LPARAM>(label.c_str()));
    SendMessageW(button, TB_ADDBUTTONSW, 1, reinterpret_cast<LPARAM>(&item));
    state->grid_button = button;
    // list would paint over the overlapping sibling
    SetWindowLongPtrW(list, GWL_STYLE, GetWindowLongPtrW(list, GWL_STYLE) | WS_CLIPSIBLINGS);
    LoadGridIcon(list, state);
    ApplyFrame(state->hooks, button);
    LayoutGridButton(list, state);
}

void SetAllListGridLines(bool shown)
{
    if (g_grid_lines != shown)
    {
        ApplyGridLines(shown);
    }
}

bool AllListGridLines()
{
    return g_grid_lines;
}

LRESULT DrawListGrid(HWND list, NMLVCUSTOMDRAW* draw, LRESULT result)
{
    ListState* state = StateOf(list);
    if (!state || !g_grid_lines || !draw)
    {
        return result;
    }
    switch (draw->nmcd.dwDrawStage)
    {
    case CDDS_PREPAINT:
        ReadGridEdges(list, state);
        state->grid_rows = state->model.Direct(list, LVM_ISGROUPVIEWENABLED, 0, 0) != 0;
        return result | CDRF_NOTIFYPOSTPAINT | (state->grid_rows ? CDRF_NOTIFYITEMDRAW : 0);
    case CDDS_ITEMPREPAINT:
        return state->grid_rows ? result | CDRF_NOTIFYPOSTPAINT : result;
    case CDDS_ITEMPOSTPAINT:
        {
            RECT row = {};
            if (state->grid_rows && ItemBounds(list, state, static_cast<int>(draw->nmcd.dwItemSpec), &row))
            {
                PaintGrid(list, state, draw->nmcd.hdc, row, row.bottom - 1, row.bottom - row.top);
            }
            return result;
        }
    case CDDS_POSTPAINT:
        if (state->grid_rows)
        {
            PaintGridTail(list, state, draw->nmcd.hdc);
        }
        else
        {
            PaintGridOnce(list, state, draw->nmcd.hdc);
        }
        return result;
    default:
        return result;
    }
}

bool DrawDialogListView(NMHDR* header, LRESULT* result)
{
    const ListState* state = header && header->code == NM_CUSTOMDRAW ? StateOf(header->hwndFrom) : nullptr;
    if (!state || !state->dialog_keys)
    {
        return false;
    }
    auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(header);
    const ListColors colors = ColorsOf();
    LRESULT rows = CDRF_DODEFAULT;
    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
    {
        rows = CDRF_NOTIFYITEMDRAW;
    }
    else if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
    {
        draw->nmcd.uItemState &= ~(CDIS_FOCUS | CDIS_HOT);
        if (!(draw->nmcd.uItemState & CDIS_SELECTED))
        {
            draw->clrText = colors.text;
            draw->clrTextBk = SurfaceColor(header->hwndFrom, colors);
        }
    }
    *result = DrawListGrid(header->hwndFrom, draw, rows);
    return true;
}

bool ListTypeSearchActive(HWND list)
{
    const ListState* state = StateOf(list);
    return state && TypedTextAlive(state);
}

void SetListColumns(HWND list, const std::vector<ListColumn>& columns)
{
    if (!list)
    {
        return;
    }
    const UINT dpi = WindowDpi(list);
    const int existing = ColumnCount(list);
    HWND header = ListView_GetHeader(list);
    const ListState* model_state = StateOf(list);
    PaintBatch batch(list, -1);
    const auto position = [&](int subitem) { return model_state ? model_state->model.Position(subitem) : subitem; };
    for (size_t index = 0; index < columns.size(); ++index)
    {
        const ListColumn& column = columns[index];
        LVCOLUMNW item = {};
        item.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;
        item.pszText = const_cast<wchar_t*>(column.title.c_str());
        item.fmt = column.format;
        // the first column holds the item text and state and is always shown
        const bool shown = index == 0 || (column.visible && column.available);
        item.cx = shown ? Scale(column.width_dip > 0 || index != 0 ? column.width_dip : column.default_width_dip, dpi) : 0;
        item.iSubItem = static_cast<int>(index);
        if (static_cast<int>(index) < existing)
        {
            // format changes clear the sort arrow, restore it from app sort state
            HDITEMW sort = {};
            sort.mask = HDI_FORMAT;
            const int before = position(static_cast<int>(index));
            const int arrow = header && before >= 0 && Header_GetItem(header, before, &sort) ? sort.fmt & (HDF_SORTUP | HDF_SORTDOWN) : 0;
            ListView_SetColumn(list, static_cast<int>(index), &item);
            const int after = position(static_cast<int>(index));
            if (arrow && after >= 0 && Header_GetItem(header, after, &sort) && !(sort.fmt & arrow))
            {
                sort.fmt |= arrow;
                Header_SetItem(header, after, &sort);
            }
        }
        else
        {
            ListView_InsertColumn(list, static_cast<int>(index), &item);
        }
    }
    for (int index = existing - 1; index >= static_cast<int>(columns.size()); --index)
    {
        ListView_DeleteColumn(list, index);
    }
    if (ListState* state = StateOf(list))
    {
        state->columns = columns;
        if (!state->columns.empty())
        {
            state->columns[0].visible = true;
            state->columns[0].available = true;
        }
        state->shown_dip.assign(columns.size(), kDefaultWidthDip);
        for (size_t index = 0; index < columns.size(); ++index)
        {
            state->shown_dip[index] = columns[index].width_dip > 0 ? columns[index].width_dip : columns[index].default_width_dip;
        }
    }
}

bool ListColumnRect(HWND list, int subitem, RECT* rect)
{
    const ListState* state = StateOf(list);
    const int position = state ? state->model.Position(subitem) : subitem;
    HWND header = list ? ListView_GetHeader(list) : nullptr;
    if (!rect || position < 0 || !header || !Header_GetItemRect(header, position, rect))
    {
        return false;
    }
    MapWindowPoints(header, list, reinterpret_cast<POINT*>(rect), 2);
    return true;
}

std::vector<ListColumn> ListColumns(HWND list)
{
    ListState* state = StateOf(list);
    const int count = ColumnCount(list);
    std::vector<ListColumn> columns = state ? state->columns : std::vector<ListColumn>();
    columns.resize(static_cast<size_t>(std::max(count, 0)));
    if (state)
    {
        RememberWidths(list, state);
    }
    const UINT dpi = WindowDpi(list);
    for (int index = 0; index < count; ++index)
    {
        ListColumn& column = columns[static_cast<size_t>(index)];
        const int width = ListView_GetColumnWidth(list, index);
        if (column.title.empty())
        {
            column.title = ColumnTitle(list, index);
        }
        column.visible = width > 0;
        column.width_dip = width > 0 ? Unscale(width, dpi) : state ? state->shown_dip[static_cast<size_t>(index)]
                                                                   : column.width_dip;
    }
    return columns;
}

void ResetListColumns(HWND list)
{
    ListState* state = StateOf(list);
    if (!state || state->columns.empty())
    {
        return;
    }
    std::vector<ListColumn> columns = state->columns;
    for (ListColumn& column : columns)
    {
        column.width_dip = column.default_width_dip;
        column.visible = true;
    }
    SetListColumns(list, columns);
}

int FitListColumn(HWND list, int column)
{
    // unavailable columns stay hidden
    if (const ListState* state = StateOf(list); state && column >= 0 && static_cast<size_t>(column) < state->columns.size() && !state->columns[static_cast<size_t>(column)].available)
    {
        return ListView_GetColumnWidth(list, column);
    }
    const std::wstring title = ColumnTitle(list, column);
    // the native fit and the final width would each paint the columns right of this one
    PaintBatch batch(list, column);
    ListView_SetColumnWidth(list, column, LVSCW_AUTOSIZE);
    int width = ListView_GetColumnWidth(list, column);
    if (GetWindowLongPtrW(list, GWL_STYLE) & LVS_OWNERDATA)
    {
        const auto [all, shown] = CellTextWidths(list, column);
        width = std::max(width, width - shown + all);
    }
    width = std::max(width, ListView_GetStringWidth(list, title.c_str()) + Scale(16, WindowDpi(list)));
    ListView_SetColumnWidth(list, column, width);
    return width;
}

std::wstring SelectedRowsText(HWND list)
{
    const int count = list ? ColumnCount(list) : 0;
    std::vector<int> order(static_cast<size_t>(std::max(count, 0)));
    if (count <= 0 || !ListView_GetColumnOrderArray(list, count, order.data()))
    {
        return {};
    }
    std::vector<int> shown;
    for (const int column : order)
    {
        if (ListView_GetColumnWidth(list, column) > 0)
        {
            shown.push_back(column);
        }
    }
    const ListState* state = StateOf(list);
    if (state)
    {
        HookParent(list, state);
    }
    const auto next = [&](int row) {
        return static_cast<int>(state ? state->model.Direct(list, LVM_GETNEXTITEM, static_cast<WPARAM>(row), MAKELPARAM(LVNI_SELECTED, 0)) : ListView_GetNextItem(list, row, LVNI_SELECTED));
    };
    std::vector<wchar_t> buffer;
    std::wstring text;
    std::wstring cell;
    bool first = true;
    for (int row = shown.empty() ? -1 : next(-1); row >= 0; row = next(row))
    {
        text.append(first ? L"" : L"\r\n");
        first = false;
        for (size_t index = 0; index < shown.size(); ++index)
        {
            ReadCell(list, state, row, shown[index], &buffer, &cell);
            if (!Alive(list, state))
            {
                return text;
            }
            std::replace_if(cell.begin(), cell.end(), [](wchar_t character) { return character == L'\t' || character == L'\r' || character == L'\n'; }, L' ');
            text.append(index ? L"\t" : L"").append(cell);
        }
    }
    return text;
}

void SelectAllListItems(HWND list)
{
    if (list && !(GetWindowLongPtrW(list, GWL_STYLE) & LVS_SINGLESEL))
    {
        ListView_SetItemState(list, -1, LVIS_SELECTED, LVIS_SELECTED);
    }
}

int ContextMenuRow(HWND list, LPARAM screen_point, POINT* screen)
{
    *screen = {GET_X_LPARAM(screen_point), GET_Y_LPARAM(screen_point)};
    if (screen->x != -1 || screen->y != -1)
    {
        LVHITTESTINFO hit = {};
        hit.pt = *screen;
        ScreenToClient(list, &hit.pt);
        return ListView_HitTest(list, &hit);
    }
    int row = ListView_GetNextItem(list, -1, LVNI_FOCUSED | LVNI_SELECTED);
    row = row >= 0 ? row : ListView_GetNextItem(list, -1, LVNI_SELECTED);
    RECT rect = {};
    if (row < 0 || !ListView_EnsureVisible(list, row, FALSE) || !ListView_GetItemRect(list, row, &rect, LVIR_LABEL))
    {
        rect = {};
        GetClientRect(ListView_GetHeader(list), &rect);
    }
    *screen = {rect.left, rect.bottom};
    ClientToScreen(list, screen);
    return row;
}

bool ListViewItemData(HWND list, int row, LPARAM* data)
{
    LVITEMW item = {};
    item.mask = LVIF_PARAM;
    item.iItem = row;
    if (!list || row < 0 || !data || !ListView_GetItem(list, &item))
    {
        return false;
    }
    *data = item.lParam;
    return true;
}

bool SelectedListViewData(HWND list, LPARAM* data)
{
    return list && ListViewItemData(list, ListView_GetNextItem(list, -1, LVNI_SELECTED), data);
}

int FindListViewItemByData(HWND list, LPARAM data)
{
    LVFINDINFOW find = {};
    find.flags = LVFI_PARAM;
    find.lParam = data;
    return list ? ListView_FindItem(list, -1, &find) : -1;
}

} // namespace win32ui
