// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/dialog/dialog_layout.h"
#include "win32ui/dialog/dialog_resizer.h"
#include "win32ui/gdi/fonts.h"
#include "win32ui/window_metrics.h"

#include <windows.h>

#include <initializer_list>
#include <vector>

namespace win32ui
{

// registers the shared DialogWindow class once, all dialogs use theme
// icon is a resource of instance, or 0 for no caption icon
bool RegisterDialogClass(HINSTANCE instance, const wchar_t* class_name, const DialogTheme* theme, int icon);

// counts nested dialogs and common file dialogs
// dont destroy a window while a modal loop on the stack can use it
class ModalScope
{
  public:
    ModalScope();
    ~ModalScope();
    ModalScope(const ModalScope&) = delete;
    ModalScope& operator=(const ModalScope&) = delete;
};
int ModalDepth();
void CancelOpenDialogs();

// themed, owner centered DIALOGEX dialog from the executable, with monitor DPI and IsDialogMessage navigation
// dialog manager uses the UI font and registered class, OnCreate adds runtime controls
class DialogWindow
{
  public:
    virtual ~DialogWindow() = default;
    // returns the End result, or IDCANCEL if window creation fails
    INT_PTR RunModal(HWND owner, const wchar_t* template_name);
    // owner must pass messages through IsDialogMessage, End destroys the modeless window
    bool OpenModeless(HWND owner, const wchar_t* template_name);
    void End(INT_PTR result);
    HWND window() const noexcept
    {
        return hwnd_;
    }

  protected:
    virtual void OnCreate() = 0;
    virtual void OnLayout(int width, int height) = 0;
    // unhandled IDOK and IDCANCEL end the dialog with that id, closing uses IDCANCEL
    virtual bool OnCommand(int id, int code, HWND control);
    virtual LRESULT OnNotify(NMHDR* header, bool* handled);
    // called before handling every message except WM_INITDIALOG, true when handled
    virtual bool OnMessage(UINT message, WPARAM wparam, LPARAM lparam, LRESULT* result);
    // initial focus control, null or disabled falls back to the default button
    virtual HWND initial_focus() const
    {
        return nullptr;
    }
    // called after WM_NCDESTROY, object may delete itself here
    virtual void OnDestroyed()
    {
    }

    HWND hwnd() const noexcept
    {
        return hwnd_;
    }
    // theme passed to RegisterDialogClass, or null
    static const DialogTheme* theme() noexcept;
    // object for a window of the registered dialog class, or null
    static DialogWindow* FromWindow(HWND window) noexcept;
    const FontSet& fonts() const noexcept
    {
        return fonts_;
    }
    UINT dpi() const noexcept
    {
        return fonts_.dpi();
    }
    int Px(int dip) const
    {
        return Scale(dip, dpi());
    }
    // control height or row pitch in DIPs, grown for fonts above 9 pt
    int TextPx(int dip) const
    {
        return Px(fonts_.Grow(dip));
    }
    void Relayout();
    // set moving or stretching template controls from OnCreate, opening layout is the base
    void Anchor(std::initializer_list<AnchorRule> rules);
    friend void AnchorDialogControls(HWND dialog, std::initializer_list<AnchorRule> rules);

    // resizable template dialogs use opening size unless OnCreate sets a minimum
    int min_width_dip_ = 320;
    int min_height_dip_ = 160;
    // ownerless dialogs center on this window, or the work area when null
    HWND place_on_ = nullptr;

  private:
    friend bool RegisterDialogClass(HINSTANCE instance, const wchar_t* class_name, const DialogTheme* theme, int icon);
    friend void EndDialogWindow(HWND dialog, INT_PTR result);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT DefaultProc(UINT message, WPARAM wparam, LPARAM lparam);
    // sets UI font on the dialog and controls with no font, previous, or the template font
    void ApplyFonts(HFONT previous);
    // sets fonts on OnCreate controls and themes edit borders
    void FinishControls(const std::vector<HWND>& from_template);
    bool CreateTemplateWindow(HWND root_owner, const wchar_t* template_name);
    void InitTemplate();
    void ScaleControls(UINT from_dpi, UINT to_dpi);
    void CenterOnOwner();
    void FocusInitial();

    HWND hwnd_ = nullptr;
    FontSet fonts_;
    // font created by the dialog manager from the template
    HFONT template_font_ = nullptr;
    std::vector<AnchorRule> anchors_;
    DialogResizer resizer_;
    bool created_ = false;
    bool done_ = false;
    bool modeless_ = false;
    INT_PTR result_ = IDCANCEL;
};

// runs a DialogBox style procedure, use EndDialogWindow instead of EndDialog
// WM_INITDIALOG receives param
INT_PTR RunProcDialog(HWND owner, const wchar_t* template_name, DLGPROC proc, LPARAM param);
// modeless and ownerless, centered on place_on, window owns its backing object
HWND OpenProcDialog(const wchar_t* template_name, DLGPROC proc, LPARAM param, HWND place_on);
void EndDialogWindow(HWND dialog, INT_PTR result);
// DialogWindow::Anchor for procedures, call from WM_INITDIALOG
void AnchorDialogControls(HWND dialog, std::initializer_list<AnchorRule> rules);

} // namespace win32ui
