set(WIN32UI_COMPILE_OPTIONS /W4 /sdl /guard:cf /utf-8 CACHE INTERNAL "")

function(win32ui_app_settings name)
    add_library(${name} INTERFACE)
    target_compile_features(${name} INTERFACE cxx_std_20)
    target_compile_definitions(${name} INTERFACE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        ${ARGN}
    )
    if (MSVC)
        target_compile_options(${name} INTERFACE ${WIN32UI_COMPILE_OPTIONS})
        target_link_options(${name} INTERFACE
            /GUARD:CF
            /DEBUG
            $<$<CONFIG:Release>:/CETCOMPAT>
            $<$<CONFIG:Release>:/OPT:REF>
            $<$<CONFIG:Release>:/OPT:ICF>
            $<$<CONFIG:Release>:/PDBALTPATH:%_PDB%>
        )
    endif()
endfunction()
