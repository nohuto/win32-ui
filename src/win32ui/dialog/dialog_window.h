// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

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
// icon is a resource of instance
bool RegisterDialogClass(HINSTANCE instance, const wchar_t* class_name, const DialogTheme* theme, int icon);

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

class DialogWindow
{
  public:
    virtual ~DialogWindow() = default;
    // returns the End result, or IDCANCEL if window creation fails
    INT_PTR RunModal(HWND owner, const wchar_t* template_name);
    // owner must pass messages through IsDialogMessage, End destroys modeless window
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
    // theme passed to RegisterDialogClass
    static const DialogTheme* theme() noexcept;
    // object for a window of the registered dialog class
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

    int TextPx(int dip) const
    {
        return Px(fonts_.Grow(dip));
    }
    void Relayout();
    void Anchor(std::initializer_list<AnchorRule> rules);
    friend void AnchorDialogControls(HWND dialog, std::initializer_list<AnchorRule> rules);

    int min_width_dip_ = 320;
    int min_height_dip_ = 160;
    HWND place_on_ = nullptr;

  private:
    friend bool RegisterDialogClass(HINSTANCE instance, const wchar_t* class_name, const DialogTheme* theme, int icon);
    friend void EndDialogWindow(HWND dialog, INT_PTR result);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT DefaultProc(UINT message, WPARAM wparam, LPARAM lparam);
    void ApplyFonts(HFONT previous);
    void FinishControls(const std::vector<HWND>& from_template);
    bool CreateTemplateWindow(HWND root_owner, const wchar_t* template_name);
    void InitTemplate();
    void ScaleControls(UINT from_dpi, UINT to_dpi);
    void CenterOnOwner();
    void FocusInitial();

    HWND hwnd_ = nullptr;
    FontSet fonts_;
    HFONT template_font_ = nullptr;
    std::vector<AnchorRule> anchors_;
    DialogResizer resizer_;
    bool created_ = false;
    bool done_ = false;
    bool modeless_ = false;
    INT_PTR result_ = IDCANCEL;
};

INT_PTR RunProcDialog(HWND owner, const wchar_t* template_name, DLGPROC proc, LPARAM param);
HWND OpenProcDialog(const wchar_t* template_name, DLGPROC proc, LPARAM param, HWND place_on);
void EndDialogWindow(HWND dialog, INT_PTR result);
void AnchorDialogControls(HWND dialog, std::initializer_list<AnchorRule> rules);

} // namespace win32ui
