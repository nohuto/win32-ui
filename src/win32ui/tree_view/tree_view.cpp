// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/tree_view/tree_view.h"

#include "win32ui/system/strings.h"

#include <cwchar>
#include <cwctype>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

#include <windowsx.h>

namespace win32ui
{

namespace
{

constexpr UINT_PTR kTreeSubclassId = 1;
constexpr DWORD kTypeTimeoutMs = 1000;
constexpr size_t kMaxItemText = size_t{1} << 16;

struct TreeState
{
    const TreeViewHooks* hooks = nullptr;
    // class procedure, reads skip the subclass layers (about 0.7 us each per item)
    WNDPROC native = nullptr;
    std::wstring typed;
    DWORD typed_at = 0;
    // Right expanded it, its children come first while it stays selected
    HTREEITEM descend = nullptr;
    // character numpad + - * produce isnt search text
    bool skip_char = false;
    std::vector<wchar_t> text;
};

struct AttachedTree
{
    HWND tree;
    TreeState* state;
};

thread_local std::vector<AttachedTree> t_trees;

TreeState* StateOf(HWND tree)
{
    for (const AttachedTree& entry : t_trees)
    {
        if (entry.tree == tree)
        {
            return entry.state;
        }
    }
    return nullptr;
}

// hooks run app code that can destroy the tree
bool Alive(HWND tree, const TreeState* state)
{
    return StateOf(tree) == state;
}

// null for a tree of another thread, which must not run the class procedure here
WNDPROC NativeOf(HWND tree)
{
    const TreeState* state = StateOf(tree);
    return state ? state->native : nullptr;
}

LRESULT Call(HWND tree, WNDPROC native, UINT message, WPARAM wparam, LPARAM lparam)
{
    return native ? CallWindowProcW(native, tree, message, wparam, lparam) : SendMessageW(tree, message, wparam, lparam);
}

HTREEITEM Next(HWND tree, WNDPROC native, UINT code, HTREEITEM item)
{
    return reinterpret_cast<HTREEITEM>(Call(tree, native, TVM_GETNEXTITEM, code, reinterpret_cast<LPARAM>(item)));
}

HTREEITEM FirstChild(HWND tree, WNDPROC native, HTREEITEM parent)
{
    return Next(tree, native, parent ? TVGN_CHILD : TVGN_ROOT, parent);
}

std::wstring_view ReadText(HWND tree, WNDPROC native, HTREEITEM item, std::vector<wchar_t>* buffer)
{
    if (buffer->size() < MAX_PATH)
    {
        buffer->resize(MAX_PATH);
    }
    for (;;)
    {
        TVITEMW value = {};
        value.mask = TVIF_TEXT;
        value.hItem = item;
        value.pszText = buffer->data();
        value.cchTextMax = static_cast<int>(buffer->size());
        buffer->front() = L'\0';
        if (!item || !Call(tree, native, TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&value)) || !value.pszText || value.pszText == LPSTR_TEXTCALLBACKW)
        {
            return {};
        }
        // callback item can answer with its own buffer
        if (value.pszText != buffer->data())
        {
            return value.pszText;
        }
        const size_t length = wcsnlen(buffer->data(), buffer->size());
        // full buffer may have cut the text
        if (length + 1 < buffer->size() || buffer->size() >= kMaxItemText)
        {
            return {buffer->data(), length};
        }
        buffer->resize(buffer->size() * 2);
    }
}

bool TypedTextAlive(const TreeState* state)
{
    return !state->typed.empty() && GetTickCount() - state->typed_at <= kTypeTimeoutMs;
}

bool LoadChildren(HWND tree, TreeState* state, HTREEITEM item)
{
    if (!state->hooks || !state->hooks->load_children)
    {
        return true;
    }
    state->hooks->load_children(state->hooks->context, tree, item);
    return Alive(tree, state);
}

// children of group items included, one pass over the items
HTREEITEM Search(HWND tree, TreeState* state, HTREEITEM parent, HTREEITEM start)
{
    // [0] from start on, [1] before start
    HTREEITEM exact[2] = {};
    HTREEITEM prefix[2] = {};
    int side = start ? 1 : 0;
    const auto visit = [&](HTREEITEM item) {
        side = item == start ? 0 : side;
        const std::wstring_view text = ReadText(tree, state->native, item, &state->text);
        if (!StartsWithInsensitive(text, state->typed))
        {
            return false;
        }
        HTREEITEM* found = text.size() == state->typed.size() ? exact : prefix;
        found[side] = found[side] ? found[side] : item;
        // nothing comes before an exact match from start on
        return found == exact && side == 0;
    };
    for (HTREEITEM item = FirstChild(tree, state->native, parent); item; item = Next(tree, state->native, TVGN_NEXT, item))
    {
        if (visit(item))
        {
            return item;
        }
        // read per item, a hook can attach other hooks
        const TreeViewHooks* hooks = state->hooks;
        if (!hooks || !hooks->is_group)
        {
            continue;
        }
        const bool group = hooks->is_group(hooks->context, tree, item);
        if (!Alive(tree, state))
        {
            return nullptr;
        }
        for (HTREEITEM child = group ? FirstChild(tree, state->native, item) : nullptr; child; child = Next(tree, state->native, TVGN_NEXT, child))
        {
            if (visit(child))
            {
                return child;
            }
        }
    }
    for (const HTREEITEM found : {exact[0], exact[1], prefix[0], prefix[1]})
    {
        if (found)
        {
            return found;
        }
    }
    return nullptr;
}

bool TypeToSelect(HWND tree, TreeState* state, wchar_t character)
{
    if (std::exchange(state->skip_char, false) && (character == L'+' || character == L'-' || character == L'*'))
    {
        return true;
    }
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
    if (state->typed.empty())
    {
        return true;
    }
    const HTREEITEM selected = Next(tree, state->native, TVGN_CARET, nullptr);
    const bool descend = selected && selected == state->descend;
    HTREEITEM target = nullptr;
    if (descend)
    {
        if (!LoadChildren(tree, state, selected))
        {
            return true;
        }
        target = Search(tree, state, selected, nullptr);
    }
    if (!target && Alive(tree, state))
    {
        target = Search(tree, state, selected ? Next(tree, state->native, TVGN_PARENT, selected) : nullptr, selected);
    }
    if (!target && !descend && selected && Alive(tree, state))
    {
        if (!LoadChildren(tree, state, selected))
        {
            return true;
        }
        target = Search(tree, state, selected, nullptr);
    }
    if (target && Alive(tree, state))
    {
        state->descend = nullptr;
        TreeView_SelectItem(tree, target);
        TreeView_EnsureVisible(tree, target);
    }
    return true;
}

LRESULT CALLBACK TreeProc(HWND tree, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data)
{
    auto* state = reinterpret_cast<TreeState*>(data);
    switch (message)
    {
    case WM_KEYDOWN:
        state->skip_char = wparam == VK_ADD || wparam == VK_SUBTRACT || wparam == VK_MULTIPLY;
        // moving starts new typed text, only Right on a collapsed item descends
        if (wparam == VK_RIGHT || wparam == VK_LEFT || wparam == VK_UP || wparam == VK_DOWN || wparam == VK_HOME || wparam == VK_END || wparam == VK_PRIOR || wparam == VK_NEXT)
        {
            state->typed.clear();
            const HTREEITEM selected = wparam == VK_RIGHT ? Next(tree, state->native, TVGN_CARET, nullptr) : nullptr;
            const bool collapsed = selected && !(Call(tree, state->native, TVM_GETITEMSTATE, reinterpret_cast<WPARAM>(selected), TVIS_EXPANDED) & TVIS_EXPANDED);
            state->descend = collapsed ? selected : nullptr;
        }
        else if (wparam == VK_MULTIPLY && state->hooks && state->hooks->expand_all)
        {
            const HTREEITEM selected = Next(tree, state->native, TVGN_CARET, nullptr);
            if (selected && state->hooks->expand_all(state->hooks->context, tree, selected))
            {
                return 0;
            }
        }
        // space inside typed text, not a check box toggle
        else if (wparam == VK_SPACE && TypedTextAlive(state) && GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_MENU) >= 0)
        {
            return 0;
        }
        break;
    case WM_CHAR:
        if (TypeToSelect(tree, state, static_cast<wchar_t>(wparam)))
        {
            return 0;
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(tree, TreeProc, kTreeSubclassId);
        std::erase_if(t_trees, [&](const AttachedTree& entry) { return entry.state == state; });
        delete state;
        break;
    default:
        break;
    }
    return DefSubclassProc(tree, message, wparam, lparam);
}

}

bool AttachTreeView(HWND tree, const TreeViewHooks* hooks)
{
    if (!tree)
    {
        return false;
    }
    if (TreeState* state = StateOf(tree))
    {
        state->hooks = hooks;
        return true;
    }
    auto state = std::make_unique<TreeState>();
    state->hooks = hooks;
    state->native = reinterpret_cast<WNDPROC>(GetClassLongPtrW(tree, GCLP_WNDPROC));
    t_trees.reserve(t_trees.size() + 1);
    if (!SetWindowSubclass(tree, TreeProc, kTreeSubclassId, reinterpret_cast<DWORD_PTR>(state.get())))
    {
        return false;
    }
    t_trees.push_back({tree, state.release()});
    TreeView_SetExtendedStyle(tree, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
    return true;
}

bool TreeTypeSearchActive(HWND tree)
{
    const TreeState* state = StateOf(tree);
    return state && TypedTextAlive(state);
}

std::wstring TreeItemText(HWND tree, HTREEITEM item)
{
    std::vector<wchar_t> buffer;
    return std::wstring(ReadText(tree, NativeOf(tree), item, &buffer));
}

bool TreeItemData(HWND tree, HTREEITEM item, LPARAM* data)
{
    TVITEMW value = {};
    value.mask = TVIF_PARAM;
    value.hItem = item;
    if (!item || !Call(tree, NativeOf(tree), TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&value)))
    {
        return false;
    }
    *data = value.lParam;
    return true;
}

HTREEITEM FindTreeChild(HWND tree, HTREEITEM parent, std::wstring_view text)
{
    const WNDPROC native = NativeOf(tree);
    std::vector<wchar_t> buffer;
    for (HTREEITEM child = FirstChild(tree, native, parent); child; child = Next(tree, native, TVGN_NEXT, child))
    {
        if (EqualsInsensitive(ReadText(tree, native, child, &buffer), text))
        {
            return child;
        }
    }
    return nullptr;
}

HTREEITEM ContextMenuTreeItem(HWND tree, LPARAM screen_point, POINT* screen)
{
    POINT point = {GET_X_LPARAM(screen_point), GET_Y_LPARAM(screen_point)};
    HTREEITEM item = nullptr;
    if (point.x != -1 || point.y != -1)
    {
        TVHITTESTINFO hit = {};
        hit.pt = point;
        ScreenToClient(tree, &hit.pt);
        item = TreeView_HitTest(tree, &hit);
    }
    else
    {
        item = TreeView_GetSelection(tree);
        RECT label = {};
        if (item)
        {
            TreeView_EnsureVisible(tree, item);
        }
        if (!item || !TreeView_GetItemRect(tree, item, &label, TRUE))
        {
            label = {};
        }
        point = {label.left, label.bottom};
        ClientToScreen(tree, &point);
    }
    *screen = point;
    return item;
}

void SetTreeChecksBelow(HWND tree, HTREEITEM item, bool checked)
{
    // depth first without recursion, a chain can be deep, a notification handler can destroy the tree, then every query
    // gives null and ends the walk
    for (HTREEITEM current = item ? TreeView_GetChild(tree, item) : nullptr; current;)
    {
        TreeView_SetCheckState(tree, current, checked);
        HTREEITEM next = TreeView_GetChild(tree, current);
        while (!next && current && current != item)
        {
            next = TreeView_GetNextSibling(tree, current);
            current = next ? current : TreeView_GetParent(tree, current);
        }
        current = next;
    }
}

}
