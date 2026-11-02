// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/dialog/autocomplete.h"

#include "win32ui/dialog/autocomplete_internal.h"
#include "win32ui/gdi/gdi_cache.h"
#include "win32ui/system/strings.h"
#include "win32ui/theme/palette.h"
#include "win32ui/theme/theme.h"
#include "win32ui/window.h"
#include "win32ui/window_metrics.h"

#include <commctrl.h>
#include <shldisp.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <vssym32.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <new>
#include <unordered_set>

namespace win32ui
{

namespace
{

constexpr UINT_PTR kSubclassId = 1;
constexpr wchar_t kPopupClass[] = L"Auto-Suggest Dropdown";
// busy edit thread gives no suggestions instead of holding the autocomplete thread
constexpr UINT kSuggestTimeoutMs = 2000;

std::atomic<bool> autocomplete_enabled = true;
// size loop of the popup itself, alignment stays out of it
thread_local bool popup_sizing = false;

struct SuggestCall
{
    std::shared_ptr<const AutoCompleteSuggest> suggest;
    // in the IACList::Expand text (empty = the edit text without the appended part), out the query used
    std::wstring query;
    std::wstring last;
    bool changed = false;
    std::vector<std::wstring> result;
};

// calls on their way to the thread of an edit, the sender removes its own so a call the edit never runs (timeout, edit
// destroyed) doesnt leak and one it gets late is skipped
struct PendingCalls
{
    std::mutex lock;
    std::vector<std::shared_ptr<SuggestCall>> calls;
};

// never freed, autocomplete threads can still run while statics are destroyed at exit
PendingCalls& Pending()
{
    static PendingCalls* const pending = new PendingCalls;
    return *pending;
}

std::shared_ptr<SuggestCall> TakePending(const SuggestCall* call)
{
    PendingCalls& pending = Pending();
    const std::lock_guard lock(pending.lock);
    const auto found = std::find_if(pending.calls.begin(), pending.calls.end(), [call](const auto& item) { return item.get() == call; });
    if (found == pending.calls.end())
    {
        return nullptr;
    }
    std::shared_ptr<SuggestCall> taken = std::move(*found);
    pending.calls.erase(found);
    return taken;
}

UINT SuggestMessage()
{
    static const UINT message = RegisterWindowMessageW(L"win32ui.AutoCompleteSuggest");
    return message;
}

void RunSuggestCall(HWND edit, SuggestCall& call)
{
    if (call.query.empty())
    {
        call.query = WindowText(edit);
        DWORD start = 0;
        DWORD end = 0;
        SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        // without the appended part, it is selected up to the end
        if (end > start && end == call.query.size())
        {
            call.query.resize(start);
        }
    }
    if (call.query != call.last)
    {
        call.changed = true;
        call.result = (*call.suggest)(call.query);
    }
}

LRESULT CALLBACK EditProc(HWND edit, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR)
{
    if (message == SuggestMessage())
    {
        const std::shared_ptr<SuggestCall> call = TakePending(reinterpret_cast<const SuggestCall*>(lparam));
        if (!call)
        {
            return FALSE;
        }
        RunSuggestCall(edit, *call);
        return TRUE;
    }
    if (message == WM_NCDESTROY)
    {
        RemoveWindowSubclass(edit, EditProc, id);
    }
    return DefSubclassProc(edit, message, wparam, lparam);
}

// IAutoComplete enumerates on its own thread, one Reset and a Next per suggestion, the suggestions are read once per
// enumeration on the thread of the edit
class SuggestSource final : public IEnumString, public IACList
{
  public:
    SuggestSource(HWND edit, std::shared_ptr<const AutoCompleteSuggest> suggest)
        : edit_(edit), suggest_(std::move(suggest))
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
    {
        if (!out)
        {
            return E_POINTER;
        }
        *out = nullptr;
        if (riid == IID_IUnknown || riid == IID_IEnumString)
        {
            *out = static_cast<IEnumString*>(this);
        }
        else if (riid == IID_IACList)
        {
            *out = static_cast<IACList*>(this);
        }
        else
        {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&refs_));
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG count = static_cast<ULONG>(InterlockedDecrement(&refs_));
        if (count == 0)
        {
            delete this;
        }
        return count;
    }

    HRESULT STDMETHODCALLTYPE Next(ULONG celt, LPOLESTR* rgelt, ULONG* pceltFetched) override
    {
        if (!rgelt || (celt > 1 && !pceltFetched))
        {
            return E_POINTER;
        }
        Begin();
        ULONG fetched = 0;
        for (; fetched < celt && index_ < suggestions_.size(); ++fetched, ++index_)
        {
            const std::wstring& item = suggestions_[index_];
            // caller frees returned strings with CoTaskMemFree
            auto* buffer = static_cast<wchar_t*>(CoTaskMemAlloc((item.size() + 1) * sizeof(wchar_t)));
            if (!buffer)
            {
                for (ULONG i = 0; i < fetched; ++i)
                {
                    CoTaskMemFree(rgelt[i]);
                }
                if (pceltFetched)
                {
                    *pceltFetched = 0;
                }
                return E_OUTOFMEMORY;
            }
            std::copy_n(item.c_str(), item.size() + 1, buffer);
            rgelt[fetched] = buffer;
        }
        if (pceltFetched)
        {
            *pceltFetched = fetched;
        }
        if (fetched < celt)
        {
            current_ = false;
            return S_FALSE;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Skip(ULONG celt) override
    {
        Begin();
        if (celt > suggestions_.size() - std::min(index_, suggestions_.size()))
        {
            index_ = suggestions_.size();
            return S_FALSE;
        }
        index_ += celt;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Reset() override
    {
        Refresh();
        current_ = true;
        index_ = 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Clone(IEnumString** out) override
    {
        if (!out)
        {
            return E_POINTER;
        }
        *out = nullptr;
        auto* clone = new (std::nothrow) SuggestSource(edit_, suggest_);
        if (!clone)
        {
            return E_OUTOFMEMORY;
        }
        try
        {
            clone->suggestions_ = suggestions_;
            clone->last_ = last_;
            clone->expand_ = expand_;
        }
        catch (const std::bad_alloc&)
        {
            clone->Release();
            return E_OUTOFMEMORY;
        }
        clone->index_ = index_;
        clone->current_ = current_;
        *out = clone;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Expand(PCWSTR text) noexcept override
    {
        current_ = false;
        if (!text)
        {
            expand_.clear();
            return S_OK;
        }
        try
        {
            expand_ = text;
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
        suggestions_.clear();
        index_ = 0;
        last_.clear();
        return S_OK;
    }

  private:
    ~SuggestSource() = default;

    // Next or Skip without a Reset, switched off also in the middle of an enumeration
    void Begin()
    {
        if (!autocomplete_enabled.load(std::memory_order_relaxed))
        {
            Clear();
            current_ = false;
        }
        else if (!current_)
        {
            Refresh();
            current_ = true;
        }
    }

    void Refresh()
    {
        if (!autocomplete_enabled.load(std::memory_order_relaxed) || !*suggest_)
        {
            Clear();
            return;
        }
        auto call = std::make_shared<SuggestCall>();
        call->suggest = suggest_;
        call->query = expand_;
        call->last = last_;
        PendingCalls& pending = Pending();
        {
            const std::lock_guard lock(pending.lock);
            pending.calls.push_back(call);
        }
        DWORD_PTR ran = FALSE;
        const bool answered = SendMessageTimeoutW(edit_, SuggestMessage(), 0, reinterpret_cast<LPARAM>(call.get()), SMTO_BLOCK, kSuggestTimeoutMs, &ran) != 0;
        TakePending(call.get());
        if (!answered || !ran)
        {
            // edit is busy or gone, the next refresh asks again
            Clear();
            return;
        }
        if (call->changed)
        {
            last_ = std::move(call->query);
            suggestions_ = std::move(call->result);
            index_ = 0;
        }
    }

    void Clear() noexcept
    {
        suggestions_.clear();
        index_ = 0;
        last_.clear();
    }

    LONG refs_ = 1;
    HWND edit_;
    std::shared_ptr<const AutoCompleteSuggest> suggest_;
    std::vector<std::wstring> suggestions_;
    size_t index_ = 0;
    // suggestions_ belong to the running enumeration
    bool current_ = false;
    std::wstring last_;
    std::wstring expand_;
};

BOOL CALLBACK ThemePopup(HWND window, LPARAM);

void AlignPopup(HWND popup, WINDOWPOS* pos)
{
    HWND anchor = GetFocus();
    if (!anchor || popup_sizing)
    {
        return;
    }
    if (ClassIs(GetParent(anchor), WC_COMBOBOXW))
    {
        anchor = GetParent(anchor);
    }
    RECT box = {};
    RECT current = {};
    GetWindowRect(anchor, &box);
    GetWindowRect(popup, &current);
    if ((pos->flags & SWP_NOSIZE) == 0)
    {
        pos->cx = box.right - box.left;
    }
    if ((pos->flags & SWP_NOMOVE) == 0)
    {
        const int height = (pos->flags & SWP_NOSIZE) ? current.bottom - current.top : pos->cy;
        pos->x = box.left;
        pos->y = pos->y < box.top ? box.top - height : box.bottom;
    }
}

LRESULT CALLBACK PopupProc(HWND popup, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR)
{
    switch (message)
    {
    case WM_NCDESTROY:
        RemoveWindowSubclass(popup, PopupProc, id);
        break;
    case WM_WINDOWPOSCHANGED:
        if (reinterpret_cast<WINDOWPOS*>(lparam)->flags & SWP_SHOWWINDOW)
        {
            ThemePopup(popup, 0);
        }
        break;
    case WM_ENTERSIZEMOVE:
    case WM_EXITSIZEMOVE:
        popup_sizing = message == WM_ENTERSIZEMOVE;
        break;
    case WM_WINDOWPOSCHANGING:
        AlignPopup(popup, reinterpret_cast<WINDOWPOS*>(lparam));
        break;
    case WM_DRAWITEM:
        {
            // suggestions are owner drawn, in system colors without this
            auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
            if (draw->CtlType != ODT_LISTVIEW)
            {
                break;
            }
            const ThemeColors& colors = CurrentThemeColors();
            const bool selected = (draw->itemState & ODS_SELECTED) != 0;
            FillRect(draw->hDC, &draw->rcItem, CachedBrush(selected ? colors.selection : colors.surface));
            wchar_t text[1024] = {};
            ListView_GetItemText(draw->hwndItem, draw->itemID, 0, text, static_cast<int>(std::size(text)));
            RECT rect = draw->rcItem;
            rect.left += Scale(4, DpiForWindow(popup));
            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, selected ? colors.selection_text : colors.text);
            DrawTextW(draw->hDC, text, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
            return TRUE;
        }
    case WM_ERASEBKGND:
        {
            RECT rect = {};
            GetClientRect(popup, &rect);
            FillRect(reinterpret_cast<HDC>(wparam), &rect, ThemeBrush(ThemeFill::surface));
            return TRUE;
        }
    default:
        break;
    }
    return DefSubclassProc(popup, message, wparam, lparam);
}

LRESULT CALLBACK SizeBoxProc(HWND box, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR)
{
    switch (message)
    {
    case WM_NCDESTROY:
        RemoveWindowSubclass(box, SizeBoxProc, id);
        break;
    case WM_ERASEBKGND:
        return TRUE;
    case WM_PAINT:
        {
            PAINTSTRUCT ps = {};
            HDC dc = BeginPaint(box, &ps);
            RECT rect = {};
            GetClientRect(box, &rect);
            FillRect(dc, &rect, ThemeBrush(ThemeFill::surface));
            if (HTHEME theme = OpenThemeData(box, VSCLASS_STATUS))
            {
                SIZE grip = {};
                GetThemePartSize(theme, dc, SP_GRIPPER, 0, &rect, TS_DRAW, &grip);
                rect.left = rect.right - grip.cx;
                rect.top = rect.bottom - grip.cy;
                DrawThemeBackground(theme, dc, SP_GRIPPER, 0, &rect, nullptr);
                CloseThemeData(theme);
            }
            EndPaint(box, &ps);
            return 0;
        }
    default:
        break;
    }
    return DefSubclassProc(box, message, wparam, lparam);
}

BOOL CALLBACK SubclassPopup(HWND window, LPARAM)
{
    if (ClassIs(window, kPopupClass))
    {
        EnsureSubclass(window, PopupProc, kSubclassId);
    }
    return TRUE;
}

BOOL CALLBACK ThemePopup(HWND window, LPARAM)
{
    if (!ClassIs(window, kPopupClass))
    {
        return TRUE;
    }
    ApplyThemeToWindow(window);
    EnsureSubclass(window, PopupProc, kSubclassId);
    EnumChildWindows(
        window,
        [](HWND child, LPARAM) -> BOOL {
            // theme walk gave the list the panel color
            if (ClassIs(child, WC_LISTVIEWW))
            {
                ListView_SetBkColor(child, CurrentThemeColors().surface);
            }
            else if (ClassIs(child, WC_SCROLLBARW) && (GetWindowLongPtrW(child, GWL_STYLE) & (SBS_SIZEBOX | SBS_SIZEGRIP)))
            {
                EnsureSubclass(child, SizeBoxProc, kSubclassId);
            }
            return TRUE;
        },
        0
    );
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    return TRUE;
}

}

IEnumString* CreateSuggestSource(HWND edit, AutoCompleteSuggest suggest)
{
    EnsureSubclass(edit, EditProc, kSubclassId);
    return new SuggestSource(edit, std::make_shared<const AutoCompleteSuggest>(std::move(suggest)));
}

bool AttachAutoComplete(HWND edit, AutoCompleteSuggest suggest)
{
    COMBOBOXINFO combo = {sizeof(combo)};
    if (GetComboBoxInfo(edit, &combo) && combo.hwndItem)
    {
        edit = combo.hwndItem;
    }
    if (!edit)
    {
        return false;
    }
    IEnumString* source = CreateSuggestSource(edit, std::move(suggest));
    IAutoComplete2* autocomplete = nullptr;
    if (FAILED(CoCreateInstance(CLSID_AutoComplete, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&autocomplete))))
    {
        source->Release();
        return false;
    }
    const HRESULT hr = autocomplete->Init(edit, source, nullptr, nullptr);
    source->Release();
    if (SUCCEEDED(hr))
    {
        autocomplete->SetOptions(ACO_AUTOSUGGEST | ACO_AUTOAPPEND | ACO_UPDOWNKEYDROPSLIST | ACO_USETAB);
        // Init created the popup on this thread, subclassed before its first paint
        EnumThreadWindows(GetCurrentThreadId(), SubclassPopup, 0);
    }
    // edit keeps the autocomplete object alive
    autocomplete->Release();
    return SUCCEEDED(hr);
}

void SetAutoCompleteEnabled(bool enabled) noexcept
{
    autocomplete_enabled.store(enabled, std::memory_order_relaxed);
}

void ApplyAutoCompleteTheme()
{
    EnumThreadWindows(GetCurrentThreadId(), ThemePopup, 0);
}

std::vector<std::wstring> SuggestComboPaths(HWND combo, std::wstring_view text)
{
    const size_t separator = text.find_last_of(L'\\');
    const std::wstring_view parent = separator == std::wstring_view::npos ? std::wstring_view() : text.substr(0, separator + 1);
    std::vector<std::wstring> items;
    std::unordered_set<std::wstring> seen;
    const int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
    for (int index = 0; index < count; ++index)
    {
        const LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, index, 0);
        if (length <= static_cast<LRESULT>(parent.size()))
        {
            continue;
        }
        std::wstring path(static_cast<size_t>(length), L'\0');
        SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(path.data()));
        if (!StartsWithInsensitive(path, parent))
        {
            continue;
        }
        path.resize(std::min(path.size(), path.find(L'\\', parent.size())));
        if (seen.insert(ToLower(path)).second)
        {
            items.push_back(std::move(path));
        }
    }
    return items;
}

}
