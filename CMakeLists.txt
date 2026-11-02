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
        src/win32ui/dialog/autocomplete.cpp
        src/win32ui/dialog/autocomplete_internal.h
        src/win32ui/dialog/check_list.cpp
        src/win32ui/dialog/combo_box.cpp
        src/win32ui/dialog/dialog_layout.cpp
        src/win32ui/dialog/dialog_layout_internal.h
        src/win32ui/dialog/dialog_resizer.cpp
        src/win32ui/dialog/dialog_window.cpp
        src/win32ui/dialog/dialog_window_internal.h
        src/win32ui/dialog/edit.cpp
        src/win32ui/dialog/file_dialog.cpp
        src/win32ui/dialog/file_dialog_internal.h
        src/win32ui/dialog/font_picker.cpp
        src/win32ui/dialog/message_dialog.cpp
        src/win32ui/dialog/table_dialog.cpp
        src/win32ui/dialog/text_prompt.cpp
        src/win32ui/dialog/tooltip.cpp
        src/win32ui/focus.cpp
        src/win32ui/gdi/buffered_paint.cpp
        src/win32ui/gdi/fonts.cpp
        src/win32ui/gdi/gdi_cache.cpp
        src/win32ui/gdi/icon_loader.cpp
        src/win32ui/gdi/icon_loader_internal.h
        src/win32ui/json/reader.cpp
        src/win32ui/json/writer.cpp
        src/win32ui/list_view/column_model.cpp
        src/win32ui/list_view/column_model_internal.h
        src/win32ui/list_view/list_view.cpp
        src/win32ui/list_view/list_view_internal.h
        src/win32ui/list_view/sort.cpp
        src/win32ui/menu/menu_bar.cpp
        src/win32ui/menu/popup_menu.cpp
        src/win32ui/menu/shortcuts.cpp
        src/win32ui/splitter/splitter.cpp
        src/win32ui/splitter/splitter_internal.h
        src/win32ui/status_bar/status_bar.cpp
        src/win32ui/system/app_runtime.cpp
        src/win32ui/system/console.cpp
        src/win32ui/system/date_time.cpp
        src/win32ui/system/date_time_internal.h
        src/win32ui/system/error_text.cpp
        src/win32ui/system/file_io.cpp
        src/win32ui/system/file_io_internal.h
        src/win32ui/system/hash.cpp
        src/win32ui/system/http.cpp
        src/win32ui/system/paths.cpp
        src/win32ui/system/process_launch.cpp
        src/win32ui/system/process_token.cpp
        src/win32ui/system/process_token_internal.h
        src/win32ui/system/recent_list.cpp
        src/win32ui/system/registry_value.cpp
        src/win32ui/system/registry_value_internal.h
        src/win32ui/system/service_control.cpp
        src/win32ui/system/shell_open.cpp
        src/win32ui/system/single_instance.cpp
        src/win32ui/system/strings.cpp
        src/win32ui/system/strings_internal.h
        src/win32ui/system/worker.cpp
        src/win32ui/tab_strip/tab_strip.cpp
        src/win32ui/text/language_pack.cpp
        src/win32ui/text/language_pack_internal.h
        src/win32ui/text/text.cpp
        src/win32ui/theme/palette.cpp
        src/win32ui/theme/palette_internal.h
        src/win32ui/theme/preset_editor.cpp
        src/win32ui/theme/preset_editor_internal.h
        src/win32ui/theme/theme.cpp
        src/win32ui/theme/theme_internal.h
        src/win32ui/theme/theme_presets.cpp
        src/win32ui/theme/theme_presets_internal.h
        src/win32ui/toolbar/toolbar.cpp
        src/win32ui/tree_view/tree_view.cpp
        src/win32ui/window.cpp
        src/win32ui/window_metrics.cpp
        src/win32ui/window_placement.cpp
    PUBLIC FILE_SET HEADERS BASE_DIRS src FILES
        src/win32ui/accessible_name.h
        src/win32ui/dialog/autocomplete.h
        src/win32ui/dialog/check_list.h
        src/win32ui/dialog/combo_box.h
        src/win32ui/dialog/dialog_layout.h
        src/win32ui/dialog/dialog_metrics.h
        src/win32ui/dialog/dialog_resizer.h
        src/win32ui/dialog/dialog_window.h
        src/win32ui/dialog/edit.h
        src/win32ui/dialog/file_dialog.h
        src/win32ui/dialog/font_picker.h
        src/win32ui/dialog/message_dialog.h
        src/win32ui/dialog/table_dialog.h
        src/win32ui/dialog/text_prompt.h
        src/win32ui/dialog/tooltip.h
        src/win32ui/focus.h
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
        src/win32ui/splitter/splitter.h
        src/win32ui/status_bar/status_bar.h
        src/win32ui/system/app_runtime.h
        src/win32ui/system/clock.h
        src/win32ui/system/console.h
        src/win32ui/system/date_time.h
        src/win32ui/system/error_text.h
        src/win32ui/system/file_io.h
        src/win32ui/system/hash.h
        src/win32ui/system/http.h
        src/win32ui/system/paths.h
        src/win32ui/system/process_launch.h
        src/win32ui/system/process_token.h
        src/win32ui/system/recent_list.h
        src/win32ui/system/registry_value.h
        src/win32ui/system/service_control.h
        src/win32ui/system/shell_open.h
        src/win32ui/system/single_instance.h
        src/win32ui/system/strings.h
        src/win32ui/system/worker.h
        src/win32ui/system_api.h
        src/win32ui/tab_strip/tab_strip.h
        src/win32ui/text/language_pack.h
        src/win32ui/text/text.h
        src/win32ui/theme/palette.h
        src/win32ui/theme/preset_editor.h
        src/win32ui/theme/theme.h
        src/win32ui/theme/theme_presets.h
        src/win32ui/toolbar/toolbar.h
        src/win32ui/tree_view/tree_view.h
        src/win32ui/window.h
        src/win32ui/window_metrics.h
        src/win32ui/window_placement.h
        src/win32ui/windows_config.h
)
target_compile_features(win32ui PUBLIC cxx_std_20)
target_compile_definitions(win32ui PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0600 WINVER=0x0600)
include(cmake/Win32uiApp.cmake)
target_compile_options(win32ui PRIVATE ${WIN32UI_COMPILE_OPTIONS} $<$<BOOL:${PROJECT_IS_TOP_LEVEL}>:/WX>)
target_link_libraries(win32ui PRIVATE comctl32 comdlg32 uxtheme dwmapi oleacc shell32 ole32 userenv wtsapi32 winhttp bcrypt crypt32)

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
