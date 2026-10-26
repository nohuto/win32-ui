cmake_minimum_required(VERSION 3.30)
project(win32ui LANGUAGES CXX)

if (PROJECT_IS_TOP_LEVEL)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(win32ui STATIC)
add_library(win32ui::win32ui ALIAS win32ui)
target_sources(win32ui
    PRIVATE
        src/win32ui/accessible_name.cpp
        src/win32ui/dialog/combo_box.cpp
        src/win32ui/dialog/dialog_layout.cpp
        src/win32ui/dialog/dialog_layout_internal.h
        src/win32ui/dialog/dialog_resizer.cpp
        src/win32ui/dialog/dialog_window.cpp
        src/win32ui/dialog/dialog_window_internal.h
        src/win32ui/dialog/message_dialog.cpp
        src/win32ui/dialog/single_line_edit.cpp
        src/win32ui/gdi/buffered_paint.cpp
        src/win32ui/gdi/fonts.cpp
        src/win32ui/gdi/fonts_internal.h
        src/win32ui/gdi/gdi_cache.cpp
        src/win32ui/gdi/icon_loader.cpp
        src/win32ui/gdi/icon_loader_internal.h
        src/win32ui/hex_digit_internal.h
        src/win32ui/json/reader.cpp
        src/win32ui/json/writer.cpp
        src/win32ui/list_view/list_view.cpp
        src/win32ui/list_view/list_view_internal.h
        src/win32ui/list_view/sort.cpp
        src/win32ui/menu/menu_bar.cpp
        src/win32ui/menu/popup_menu.cpp
        src/win32ui/menu/shortcuts.cpp
        src/win32ui/process_token.cpp
        src/win32ui/splitter/splitter.cpp
        src/win32ui/splitter/splitter_internal.h
        src/win32ui/status_bar/status_bar.cpp
        src/win32ui/tab_strip/tab_strip.cpp
        src/win32ui/theme/palette.cpp
        src/win32ui/theme/palette_internal.h
        src/win32ui/theme/preset_editor.cpp
        src/win32ui/theme/preset_editor_internal.h
        src/win32ui/theme/theme.cpp
        src/win32ui/theme/theme_internal.h
        src/win32ui/theme/theme_presets.cpp
        src/win32ui/theme/theme_presets_internal.h
        src/win32ui/toolbar/toolbar.cpp
        src/win32ui/window_metrics.cpp
    PUBLIC FILE_SET HEADERS BASE_DIRS src FILES
        src/win32ui/accessible_name.h
        src/win32ui/dialog/combo_box.h
        src/win32ui/dialog/dialog_layout.h
        src/win32ui/dialog/dialog_metrics.h
        src/win32ui/dialog/dialog_resizer.h
        src/win32ui/dialog/dialog_window.h
        src/win32ui/dialog/message_dialog.h
        src/win32ui/dialog/single_line_edit.h
        src/win32ui/gdi/buffered_paint.h
        src/win32ui/gdi/fonts.h
        src/win32ui/gdi/gdi_cache.h
        src/win32ui/gdi/icon_loader.h
        src/win32ui/handle_owner.h
        src/win32ui/json/reader.h
        src/win32ui/json/writer.h
        src/win32ui/list_view/list_view.h
        src/win32ui/list_view/sort.h
        src/win32ui/menu/menu_bar.h
        src/win32ui/menu/popup_menu.h
        src/win32ui/menu/shortcuts.h
        src/win32ui/process_token.h
        src/win32ui/splitter/splitter.h
        src/win32ui/status_bar/status_bar.h
        src/win32ui/system_api.h
        src/win32ui/tab_strip/tab_strip.h
        src/win32ui/theme/palette.h
        src/win32ui/theme/preset_editor.h
        src/win32ui/theme/theme.h
        src/win32ui/theme/theme_presets.h
        src/win32ui/toolbar/toolbar.h
        src/win32ui/window_metrics.h
)
target_compile_features(win32ui PUBLIC cxx_std_20)
target_compile_definitions(win32ui PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0600 WINVER=0x0600)
target_compile_options(win32ui PRIVATE /W4 /utf-8 /sdl /guard:cf $<$<BOOL:${PROJECT_IS_TOP_LEVEL}>:/WX>)
target_link_libraries(win32ui PRIVATE comctl32 uxtheme dwmapi oleacc)

if (PROJECT_IS_TOP_LEVEL)
    file(GLOB stress_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/stress/*.cpp")
    foreach(source IN LISTS stress_sources)
        get_filename_component(name "${source}" NAME_WE)
        add_executable(${name} "${source}")
        if (EXISTS "${PROJECT_SOURCE_DIR}/stress/${name}.rc")
            target_sources(${name} PRIVATE "${PROJECT_SOURCE_DIR}/stress/${name}.rc")
        endif()
        target_compile_options(${name} PRIVATE /W4 /WX /utf-8)
        target_link_options(${name} PRIVATE "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'")
        target_link_libraries(${name} PRIVATE win32ui)
    endforeach()
endif()
