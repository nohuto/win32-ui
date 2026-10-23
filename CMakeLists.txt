cmake_minimum_required(VERSION 3.30)
project(win32ui LANGUAGES CXX)

# an app that pulls the library in sets its own runtime before FetchContent
if (PROJECT_IS_TOP_LEVEL)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(win32ui STATIC)
add_library(win32ui::win32ui ALIAS win32ui)
target_sources(win32ui
    PRIVATE
        src/gdi_cache.cpp
    PUBLIC FILE_SET HEADERS BASE_DIRS include FILES
        include/win32ui/gdi_cache.h
        include/win32ui/handle_owner.h
        include/win32ui/system_api.h
)
target_compile_features(win32ui PUBLIC cxx_std_20)
# private, an app keeps its own definitions and warning level
target_compile_definitions(win32ui PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0600 WINVER=0x0600)
target_compile_options(win32ui PRIVATE /EHsc /W4 /permissive- /utf-8 /sdl /guard:cf $<$<BOOL:${PROJECT_IS_TOP_LEVEL}>:/WX>)
target_link_libraries(win32ui PRIVATE gdi32)

if (PROJECT_IS_TOP_LEVEL)
    add_executable(resource_churn stress/resource_churn.cpp)
    target_compile_options(resource_churn PRIVATE /EHsc /W4 /WX /permissive- /utf-8)
    target_link_libraries(resource_churn PRIVATE win32ui user32)
endif()
