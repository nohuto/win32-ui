// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

#include "win32ui/dialog/dialog_window.h"

#include "win32ui/dialog/dialog_layout_internal.h"
#include "win32ui/dialog/dialog_window_internal.h"
#include "win32ui/list_view/list_view.h"
#include "win32ui/list_view/list_view_internal.h"
#include "win32ui/status_bar/status_bar.h"
#include "win32ui/system_api.h"
#include "win32ui/theme/palette.h"

#include <commctrl.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <vector>

namespace win32ui
{

namespace
{

// Windows 8.1 constant, build targets Vista
constexpr UINT kDpiChangedMessage = 0x02E0;
// DDC_DISABLE_ALL for SetDialogDpiChangeBehavior, Windows 10 1703, template dialogs scale here
constexpr DWORD kDialogDpiDisableAll = 0x0001;
// object pointer follows the dialog manager window bytes
constexpr int kObjectOffset = (DLGWINDOWEXTRA + 7) & ~7;

const wchar_t* g_class_name = nullptr;
const DialogTheme* g_theme = nullptr;
// modal frames hide caption icons, so omit the frame when using the app icon
bool g_caption_icon = false;
int g_modal_depth = 0;
// object being created, dialog manager creates the window without passing it
DialogWindow* g_creating = nullptr;

// open dialogs, modal and modeless, in creation order
std::vector<HWND>& OpenDialogs()
{
    static std::vector<HWND> dialogs;
    return dialogs;
}

// theming selects all text in editable combo boxes, preserve the dialog selection
void ApplyTheme(HWND dialog)
{
    if (!g_theme || !g_theme->apply)
    {
        return;
    }
    struct Selection
    {
        HWND edit;
        DWORD start;
        DWORD end;
    };
    std::vector<Selection> selections;
    for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
    {
        COMBOBOXINFO info = {sizeof(info)};
        if (ClassIs(child, WC_COMBOBOXW) && GetComboBoxInfo(child, &info) && info.hwndItem && info.hwndItem != child)
        {
            Selection selection = {info.hwndItem, 0, 0};
            SendMessageW(info.hwndItem, EM_GETSEL, reinterpret_cast<WPARAM>(&selection.start), reinterpret_cast<LPARAM>(&selection.end));
            selections.push_back(selection);
        }
    }
    g_theme->apply(dialog);
    for (const Selection& selection : selections)
    {
        SendMessageW(selection.edit, EM_SETSEL, selection.start, selection.end);
    }
}

// centers on the owner or work area, within the destination monitor work area
// fixed windows that dont fit keep their size and caption on screen, like dialog boxes
RECT CenteredRect(HWND owner, int width, int height, bool resizable)
{
    RECT target = {};
    if (!owner || !GetWindowRect(owner, &target))
    {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &target, 0);
    }
    RECT rect = {};
    rect.left = target.left + (target.right - target.left - width) / 2;
    rect.top = target.top + (target.bottom - target.top - height) / 2;
    rect.right = rect.left + width;
    rect.bottom = rect.top + height;
    if (resizable)
    {
        ClampToWorkArea(&rect);
        return rect;
    }
    MONITORINFO monitor = {sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), &monitor))
    {
        const RECT& work = monitor.rcWork;
        OffsetRect(&rect, std::clamp<LONG>(rect.left, work.left, std::max<LONG>(work.left, work.right - width)) - rect.left, std::clamp<LONG>(rect.top, work.top, std::max<LONG>(work.top, work.bottom - height)) - rect.top);
    }
    return rect;
}

// Return presses the default button in multiline edits without ES_WANTRETURN
// Ctrl+Return inserts a line break
bool PressDefaultOnReturn(HWND dialog, const MSG& message)
{
    if (message.message != WM_KEYDOWN || message.wParam != VK_RETURN || GetKeyState(VK_CONTROL) < 0 || !IsChild(dialog, message.hwnd))
    {
        return false;
    }
    const LONG_PTR style = GetWindowLongPtrW(message.hwnd, GWL_STYLE);
    if ((style & (ES_MULTILINE | ES_WANTRETURN)) != ES_MULTILINE || !ClassIs(message.hwnd, WC_EDITW))
    {
        return false;
    }
    const LRESULT default_id = SendMessageW(dialog, DM_GETDEFID, 0, 0);
    if (HIWORD(default_id) != DC_HASDEFID)
    {
        return false;
    }
    const WORD id = LOWORD(default_id);
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(dialog, id)));
    return true;
}

// loads executable DIALOGEX resource name with the UI font and registered class, hidden until layout
// returns empty for other template formats
std::vector<BYTE> LoadDialogTemplate(const wchar_t* name)
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const HRSRC resource = FindResourceW(instance, name, RT_DIALOG);
    const HGLOBAL loaded = resource ? LoadResource(instance, resource) : nullptr;
    const auto* data = loaded ? static_cast<const BYTE*>(LockResource(loaded)) : nullptr;
    const size_t size = data ? SizeofResource(instance, resource) : 0;
    const auto word = [&](size_t offset) { return offset + 2 <= size ? *reinterpret_cast<const WORD*>(data + offset) : WORD{0}; };
    // version 1, signature 0xFFFF, menu, class and title follow the 26 byte header, style offset is 12
    if (size < 26 || word(0) != 1 || word(2) != 0xFFFF)
    {
        return {};
    }
    const auto skip = [&](size_t offset) {
        if (word(offset) == 0xFFFF)
        {
            return offset + 4;
        }
        while (word(offset))
        {
            offset += 2;
        }
        return offset + 2;
    };
    const size_t window_class = skip(26);
    const size_t title = skip(window_class);
    const size_t font = skip(title);
    DWORD style = 0;
    std::memcpy(&style, data + 12, sizeof(style));
    size_t items = font;
    if (style & DS_SETFONT)
    {
        for (items = font + 6; word(items); items += 2)
        {
        }
        items += 2;
    }
    items = (items + 3) & ~size_t{3};
    // templates without controls end at the unpadded font name, item count offset is 16
    if (items > size && word(16) == 0)
    {
        items = size;
    }
    if (items > size)
    {
        return {};
    }

    std::vector<BYTE> patched(data, data + window_class);
    const DWORD patched_style = (style & ~WS_VISIBLE) | DS_SETFONT;
    std::memcpy(patched.data() + 12, &patched_style, sizeof(patched_style));
    const auto append = [&](const void* bytes, size_t count) {
        patched.insert(patched.end(), static_cast<const BYTE*>(bytes), static_cast<const BYTE*>(bytes) + count);
    };
    append(g_class_name, (std::wcslen(g_class_name) + 1) * sizeof(wchar_t));
    append(data + title, font - title);
    const LOGFONTW ui = UIFontLogFont(USER_DEFAULT_SCREEN_DPI);
    const WORD header[] = {static_cast<WORD>(MulDiv(std::abs(ui.lfHeight), 72, USER_DEFAULT_SCREEN_DPI)), static_cast<WORD>(ui.lfWeight), static_cast<WORD>(ui.lfItalic | (ui.lfCharSet << 8))};
    append(header, sizeof(header));
    append(ui.lfFaceName, (std::wcslen(ui.lfFaceName) + 1) * sizeof(wchar_t));
    patched.resize((patched.size() + 3) & ~size_t{3});
    append(data + items, size - items);
    return patched;
}

class ProcDialog final : public DialogWindow
{
  public:
    ProcDialog(DLGPROC proc, LPARAM param, HWND place_on = nullptr)
        : proc_(proc), param_(param)
    {
        place_on_ = place_on;
    }

    // window deletes the object on destruction
    bool owned = false;

  protected:
    void OnCreate() override
    {
        // like DialogBox, focus the first control unless the procedure returned FALSE to set focus itself
        // look it up again because WM_INITDIALOG can change tab order
        if (proc_(hwnd(), WM_INITDIALOG, reinterpret_cast<WPARAM>(GetNextDlgTabItem(hwnd(), nullptr, FALSE)), param_))
        {
            focus_ = GetNextDlgTabItem(hwnd(), nullptr, FALSE);
        }
        else
        {
            HWND focused = GetFocus();
            focus_ = IsChild(hwnd(), focused) ? focused : nullptr;
        }
    }

    void OnLayout(int, int) override
    {
    }

    bool OnMessage(UINT message, WPARAM wparam, LPARAM lparam, LRESULT* result) override
    {
        // closing a modeless dialog deletes this object with the window
        HWND dialog = hwnd();
        SetWindowLongPtrW(dialog, DWLP_MSGRESULT, 0);
        const INT_PTR handled = proc_(dialog, message, wparam, lparam);
        if (!IsWindow(dialog))
        {
            *result = 0;
            return true;
        }
        if (!handled)
        {
            return false;
        }
        // these messages return directly, others use DWLP_MSGRESULT
        const bool direct = message == WM_CHARTOITEM || message == WM_COMPAREITEM || message == WM_QUERYDRAGICON || message == WM_VKEYTOITEM ||
                            (message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC);
        *result = direct ? handled : GetWindowLongPtrW(dialog, DWLP_MSGRESULT);
        return true;
    }

    HWND initial_focus() const override
    {
        return focus_ && IsWindow(focus_) ? focus_ : nullptr;
    }

    void OnDestroyed() override
    {
        if (owned)
        {
            delete this;
        }
    }

  private:
    DLGPROC proc_ = nullptr;
    LPARAM param_ = 0;
    HWND focus_ = nullptr;
};

} // namespace

bool RegisterDialogClass(HINSTANCE instance, const wchar_t* class_name, const DialogTheme* theme, int icon)
{
    if (g_class_name)
    {
        return true;
    }
    WNDCLASSEXW window_class = {sizeof(window_class)};
    window_class.lpfnWndProc = &DialogWindow::WindowProc;
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (icon)
    {
        window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(icon));
        window_class.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(icon), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    }
    window_class.lpszClassName = class_name;
    window_class.cbWndExtra = kObjectOffset + sizeof(void*);
    if (!RegisterClassExW(&window_class))
    {
        // small icon is not shared, class would have owned it
        if (window_class.hIconSm)
        {
            DestroyIcon(window_class.hIconSm);
        }
        return false;
    }
    g_caption_icon = window_class.hIconSm != nullptr || window_class.hIcon != nullptr;
    g_class_name = class_name;
    g_theme = theme;
    return true;
}

ModalScope::ModalScope()
{
    ++g_modal_depth;
}

ModalScope::~ModalScope()
{
    --g_modal_depth;
}

int ModalDepth()
{
    return g_modal_depth;
}

void CancelOpenDialogs()
{
    for (HWND dialog : OpenDialogs())
    {
        PostMessageW(dialog, WM_CLOSE, 0, 0);
    }
}

void ReapplyThemeToDialogs()
{
    // dialogs may close or open others during theme changes
    const std::vector<HWND> dialogs = OpenDialogs();
    for (HWND dialog : dialogs)
    {
        if (!IsWindow(dialog))
        {
            continue;
        }
        ApplyTheme(dialog);
        // grid buttons and list/status tooltips are not children of dialog controls
        for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        {
            wchar_t name[32] = {};
            GetClassNameW(child, name, static_cast<int>(std::size(name)));
            if (_wcsicmp(name, WC_LISTVIEWW) == 0)
            {
                RefreshListView(child);
            }
            else if (_wcsicmp(name, STATUSCLASSNAMEW) == 0)
            {
                RefreshStatusBar(child);
            }
        }
        RedrawWindow(dialog, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
    }
}

INT_PTR RunProcDialog(HWND owner, const wchar_t* template_name, DLGPROC proc, LPARAM param)
{
    ProcDialog dialog(proc, param);
    return dialog.RunModal(owner, template_name);
}

HWND OpenProcDialog(const wchar_t* template_name, DLGPROC proc, LPARAM param, HWND place_on)
{
    auto* dialog = new ProcDialog(proc, param, place_on);
    if (!dialog->OpenModeless(nullptr, template_name))
    {
        delete dialog;
        return nullptr;
    }
    dialog->owned = true;
    return dialog->window();
}

void EndDialogWindow(HWND dialog, INT_PTR result)
{
    if (DialogWindow* window = DialogWindow::FromWindow(dialog))
    {
        window->End(result);
    }
}

void AnchorDialogControls(HWND dialog, std::initializer_list<AnchorRule> rules)
{
    if (DialogWindow* window = DialogWindow::FromWindow(dialog))
    {
        window->Anchor(rules);
    }
}

DialogWindow* DialogWindow::FromWindow(HWND window) noexcept
{
    wchar_t name[64] = {};
    if (!g_class_name || !window || !GetClassNameW(window, name, static_cast<int>(std::size(name))) || std::wcscmp(name, g_class_name) != 0)
    {
        return nullptr;
    }
    return reinterpret_cast<DialogWindow*>(GetWindowLongPtrW(window, kObjectOffset));
}

const DialogTheme* DialogWindow::theme() noexcept
{
    return g_theme;
}

void DialogWindow::FinishControls(const std::vector<HWND>& from_template)
{
    // OnCreate controls get the UI font even if they already have one
    // fonts set by OnCreate on template controls stay
    struct Pass
    {
        HFONT font;
        const std::vector<HWND>* from_template;
    } pass = {fonts_.ui(), &from_template};
    EnumChildWindows(
        hwnd_,
        [](HWND child, LPARAM parameter) -> BOOL {
            const auto* fonts = reinterpret_cast<const Pass*>(parameter);
            if (std::find(fonts->from_template->begin(), fonts->from_template->end(), child) == fonts->from_template->end())
            {
                SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(fonts->font), FALSE);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&pass)
    );
    // theme dialog edits, excluding edits inside combo boxes
    for (HWND child = GetWindow(hwnd_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
    {
        if (ClassIs(child, WC_EDITW))
        {
            AttachThemedBorder(child);
        }
    }
}

bool DialogWindow::CreateTemplateWindow(HWND root_owner, const wchar_t* template_name)
{
    if (!g_class_name)
    {
        return false;
    }
    const std::vector<BYTE> dialog = LoadDialogTemplate(template_name);
    if (dialog.empty())
    {
        return false;
    }
    min_width_dip_ = 0;
    min_height_dip_ = 0;
    fonts_.Create(WindowDpi(root_owner));
    // first window message binds the object, WM_INITDIALOG runs layout
    // dialog manager only sends it when a dialog procedure exists, so supply an empty one
    g_creating = this;
    CreateDialogIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<const DLGTEMPLATE*>(dialog.data()), root_owner, [](HWND, UINT, WPARAM, LPARAM) -> INT_PTR { return FALSE; }, 0);
    g_creating = nullptr;
    return hwnd_ != nullptr;
}

void DialogWindow::InitTemplate()
{
    OpenDialogs().push_back(hwnd_);
    if (const LONG_PTR ex_style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE); g_caption_icon && (ex_style & WS_EX_DLGMODALFRAME))
    {
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex_style & ~static_cast<LONG_PTR>(WS_EX_DLGMODALFRAME));
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    static const auto dpi_behavior = ImportProc<BOOL(WINAPI*)(HWND, DWORD, DWORD)>(L"user32.dll", "SetDialogDpiChangeBehavior");
    if (dpi_behavior)
    {
        dpi_behavior(hwnd_, kDialogDpiDisableAll, kDialogDpiDisableAll);
    }
    // dialog manager uses monitor DPI, rescale only for a DPI override
    if (WindowDpi(hwnd_) != fonts_.dpi())
    {
        fonts_.Create(WindowDpi(hwnd_));
    }
    ScaleControls(DpiForWindow(hwnd_), fonts_.dpi());
    template_font_ = reinterpret_cast<HFONT>(SendMessageW(hwnd_, WM_GETFONT, 0, 0));
    ApplyFonts(nullptr);
    if (g_theme && g_theme->prepare)
    {
        g_theme->prepare(hwnd_);
    }
    // all template controls are still visible, so controls shown later fit their text too
    FitDialogText(hwnd_);
    std::vector<HWND> from_template;
    EnumChildWindows(
        hwnd_,
        [](HWND child, LPARAM list) -> BOOL {
            reinterpret_cast<std::vector<HWND>*>(list)->push_back(child);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&from_template)
    );
    OnCreate();
    created_ = true;
    FinishControls(from_template);
    ApplyTheme(hwnd_);
    Relayout();
    if ((GetWindowLongPtrW(hwnd_, GWL_STYLE) & WS_THICKFRAME) && min_width_dip_ == 0 && min_height_dip_ == 0)
    {
        RECT client = {};
        GetClientRect(hwnd_, &client);
        min_width_dip_ = Unscale(client.right, dpi());
        min_height_dip_ = Unscale(client.bottom, dpi());
    }
    resizer_.Attach(hwnd_, anchors_);
    CenterOnOwner();
}

void DialogWindow::ScaleControls(UINT from_dpi, UINT to_dpi)
{
    if (from_dpi == 0 || from_dpi == to_dpi)
    {
        return;
    }
    const auto scale = [&](LONG value) { return MulDiv(value, static_cast<int>(to_dpi), static_cast<int>(from_dpi)); };
    for (HWND child = GetWindow(hwnd_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
    {
        RECT rect = {};
        GetWindowRect(child, &rect);
        // drop down window height includes the list
        RECT dropped = {};
        if (ClassIs(child, WC_COMBOBOXW) && SendMessageW(child, CB_GETDROPPEDCONTROLRECT, 0, reinterpret_cast<LPARAM>(&dropped)))
        {
            rect.bottom = rect.top + (dropped.bottom - dropped.top);
        }
        MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&rect), 2);
        SetWindowPos(child, nullptr, scale(rect.left), scale(rect.top), scale(rect.right - rect.left), scale(rect.bottom - rect.top), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (!created_)
    {
        RECT client = {};
        GetClientRect(hwnd_, &client);
        RECT frame = {0, 0, scale(client.right), scale(client.bottom)};
        AdjustWindowRectForDpi(&frame, static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE)), static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE)), to_dpi);
        SetWindowPos(hwnd_, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void DialogWindow::CenterOnOwner()
{
    // layout can resize the window, center after it finishes
    RECT window = {};
    GetWindowRect(hwnd_, &window);
    const bool resizable = (GetWindowLongPtrW(hwnd_, GWL_STYLE) & WS_THICKFRAME) != 0;
    HWND owner = GetWindow(hwnd_, GW_OWNER);
    const RECT centred = CenteredRect(owner ? owner : place_on_, window.right - window.left, window.bottom - window.top, resizable);
    SetWindowPos(hwnd_, nullptr, centred.left, centred.top, centred.right - centred.left, centred.bottom - centred.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

void DialogWindow::FocusInitial()
{
    // show focus rectangles and access keys regardless of how the dialog opened
    SendMessageW(hwnd_, WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS | UISF_HIDEACCEL), 0);
    HWND first = GetNextDlgTabItem(hwnd_, nullptr, FALSE);
    HWND preferred = initial_focus();
    if (!preferred || !IsWindowEnabled(preferred))
    {
        const LRESULT default_id = SendMessageW(hwnd_, DM_GETDEFID, 0, 0);
        preferred = HIWORD(default_id) == DC_HASDEFID ? GetDlgItem(hwnd_, LOWORD(default_id)) : nullptr;
    }
    SetFocus(preferred && IsWindowEnabled(preferred) ? preferred : first ? first
                                                                         : hwnd_);
}

bool DialogWindow::OpenModeless(HWND owner, const wchar_t* template_name)
{
    modeless_ = true;
    if (!CreateTemplateWindow(owner ? GetAncestor(owner, GA_ROOT) : nullptr, template_name))
    {
        return false;
    }
    ShowWindow(hwnd_, SW_SHOW);
    FocusInitial();
    return true;
}

INT_PTR DialogWindow::RunModal(HWND owner, const wchar_t* template_name)
{
    const ModalScope modal;
    HWND root_owner = owner ? GetAncestor(owner, GA_ROOT) : nullptr;
    // nested dialogs, including asynchronous failures, belong to the open dialog
    // otherwise that dialog stays usable behind them
    if (root_owner && !IsWindowEnabled(root_owner) && !OpenDialogs().empty())
    {
        root_owner = OpenDialogs().back();
    }
    if (!CreateTemplateWindow(root_owner, template_name))
    {
        return IDCANCEL;
    }
    // reenable only owners disabled here, another modal window may still need them disabled
    const bool owner_was_enabled = root_owner && IsWindowEnabled(root_owner);
    if (owner_was_enabled)
    {
        EnableWindow(root_owner, FALSE);
    }
    ShowWindow(hwnd_, SW_SHOW);
    FocusInitial();

    MSG message = {};
    while (!done_ && IsWindow(hwnd_))
    {
        const BOOL got = GetMessageW(&message, nullptr, 0, 0);
        if (got == 0)
        {
            PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (got == -1)
        {
            break;
        }
        if (PressDefaultOnReturn(hwnd_, message))
        {
            continue;
        }
        if (!IsDialogMessageW(hwnd_, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (owner_was_enabled)
    {
        EnableWindow(root_owner, TRUE);
    }
    if (root_owner)
    {
        SetActiveWindow(root_owner);
    }
    if (IsWindow(hwnd_))
    {
        DestroyWindow(hwnd_);
    }
    return result_;
}

void DialogWindow::End(INT_PTR result)
{
    result_ = result;
    done_ = true;
    if (modeless_)
    {
        DestroyWindow(hwnd_);
        return;
    }
    PostMessageW(hwnd_, WM_NULL, 0, 0);
}

void DialogWindow::Anchor(std::initializer_list<AnchorRule> rules)
{
    anchors_.assign(rules.begin(), rules.end());
}

bool DialogWindow::OnCommand(int, int, HWND)
{
    return false;
}

LRESULT DialogWindow::OnNotify(NMHDR*, bool* handled)
{
    *handled = false;
    return 0;
}

bool DialogWindow::OnMessage(UINT, WPARAM, LPARAM, LRESULT*)
{
    return false;
}

void DialogWindow::ApplyFonts(HFONT previous)
{
    SendMessageW(hwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_.ui()), FALSE);
    struct Pass
    {
        HFONT font;
        HFONT previous;
        HFONT template_font;
    } pass = {fonts_.ui(), previous, template_font_};
    EnumChildWindows(
        hwnd_,
        [](HWND child, LPARAM parameter) -> BOOL {
            const auto* fonts = reinterpret_cast<const Pass*>(parameter);
            // keep fonts chosen by the dialog for individual controls
            const HFONT current = reinterpret_cast<HFONT>(SendMessageW(child, WM_GETFONT, 0, 0));
            if (!current || current == fonts->previous || current == fonts->template_font)
            {
                SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(fonts->font), FALSE);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&pass)
    );
}

void DialogWindow::Relayout()
{
    RECT client = {};
    GetClientRect(hwnd_, &client);
    OnLayout(client.right - client.left, client.bottom - client.top);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

LRESULT CALLBACK DialogWindow::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto* self = reinterpret_cast<DialogWindow*>(GetWindowLongPtrW(hwnd, kObjectOffset));
    if (!self)
    {
        // CreateTemplateWindow set the object before the dialog manager creates the window
        self = g_creating;
        if (!self)
        {
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
        g_creating = nullptr;
        SetWindowLongPtrW(hwnd, kObjectOffset, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    if (message == WM_NCDESTROY)
    {
        LRESULT ignored = 0;
        self->OnMessage(message, wparam, lparam, &ignored);
        auto& dialogs = OpenDialogs();
        dialogs.erase(std::remove(dialogs.begin(), dialogs.end(), hwnd), dialogs.end());
        const LRESULT result = self->DefaultProc(message, wparam, lparam);
        SetWindowLongPtrW(hwnd, kObjectOffset, 0);
        self->hwnd_ = nullptr;
        self->OnDestroyed();
        return result;
    }
    return self->Handle(message, wparam, lparam);
}

LRESULT DialogWindow::DefaultProc(UINT message, WPARAM wparam, LPARAM lparam)
{
    return DefDlgProcW(hwnd_, message, wparam, lparam);
}

LRESULT DialogWindow::Handle(UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_INITDIALOG)
    {
        InitTemplate();
        // FocusInitial sets focus after the dialog becomes visible
        return FALSE;
    }
    LRESULT handled_result = 0;
    if (OnMessage(message, wparam, lparam, &handled_result))
    {
        return handled_result;
    }
    switch (message)
    {
    case WM_COMMAND:
        {
            const int id = LOWORD(wparam);
            if (OnCommand(id, HIWORD(wparam), reinterpret_cast<HWND>(lparam)))
            {
                return 0;
            }
            if (id == IDCANCEL || id == IDOK)
            {
                End(id);
                return 0;
            }
            break;
        }
    case WM_NOTIFY:
        {
            bool handled = false;
            auto* header = reinterpret_cast<NMHDR*>(lparam);
            LRESULT result = OnNotify(header, &handled);
            if (handled)
            {
                return result;
            }
            if (g_theme && g_theme->custom_draw && g_theme->custom_draw(header, &result))
            {
                return result;
            }
            if (DrawDialogListView(header, &result))
            {
                return result;
            }
            break;
        }
    case WM_CLOSE:
        // use the Escape path so caption close also gets the dialog specific IDCANCEL result
        SendMessageW(hwnd_, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
        return 0;
    case WM_ERASEBKGND:
        EraseDialogBackground(hwnd_, reinterpret_cast<HDC>(wparam), ThemeBrush(ThemeFill::background));
        return 1;
    case WM_NCHITTEST:
        if (IsOnSizeGrip(hwnd_, lparam))
        {
            return HTBOTTOMRIGHT;
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORDLG:
        if (g_theme && g_theme->control_color)
        {
            return reinterpret_cast<LRESULT>(g_theme->control_color(message, reinterpret_cast<HDC>(wparam), reinterpret_cast<HWND>(lparam)));
        }
        break;
    case WM_SIZE:
        // size messages arrive before OnCreate adds controls, first layout follows OnCreate
        if (!created_)
        {
            return 0;
        }
        resizer_.Apply(hwnd_);
        Relayout();
        if (GetWindowLongPtrW(hwnd_, GWL_STYLE) & WS_THICKFRAME)
        {
            InvalidateRect(hwnd_, nullptr, TRUE);
        }
        return 0;
    case WM_GETMINMAXINFO:
        {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            RECT rect = {0, 0, Px(min_width_dip_), Px(min_height_dip_)};
            AdjustWindowRectForDpi(&rect, static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE)), static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE)), dpi());
            info->ptMinTrackSize.x = rect.right - rect.left;
            info->ptMinTrackSize.y = rect.bottom - rect.top;
            return 0;
        }
    case kDpiChangedMessage:
        {
            const UINT previous = fonts_.dpi();
            FontSet next;
            next.Create(HIWORD(wparam));
            // keep existing fonts and layout if GDI allocation fails
            const UINT target = next.ui() ? next.dpi() : previous;
            // scale controls before changing fonts, combo boxes remeasure height with the new font
            ScaleControls(previous, target);
            resizer_.Rescale(previous, target);
            if (next.ui())
            {
                std::swap(fonts_, next);
                // next destroys previous fonts after controls switch
                ApplyFonts(next.ui());
            }
            const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            Relayout();
            return 0;
        }
    default:
        break;
    }
    return DefaultProc(message, wparam, lparam);
}

} // namespace win32ui
