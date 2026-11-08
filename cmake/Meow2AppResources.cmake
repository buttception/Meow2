# What the executables share besides code (docs/wiki/Theme.md): the app icons and the UI fonts.
#
#   meow2_app_icon(<target> <icon>)  embeds assets/icons/<icon>.ico (Windows): Explorer, the taskbar and every window
#                                    SDL and Qt open show it. Meow2 (the plain cat, Player), Meow2Editor (E badge),
#                                    Meow2Renderer (R badge).
#   meow2_app_fonts(<target>)        copies assets/fonts/ next to the executable, into fonts/, after each build; the
#                                    release zip gets one copy.

set(MEOW2_FONTS_DIR "${PROJECT_SOURCE_DIR}/assets/fonts")

function(meow2_app_icon target icon)
    if(NOT WIN32)
        return()
    endif()
    # The resource script names the icon by absolute path, so it's generated: rc.exe resolves relative names against
    # the folder it runs in, not the script's.
    set(MEOW2_ICON_FILE "${PROJECT_SOURCE_DIR}/assets/icons/${icon}.ico")
    set(script "${PROJECT_BINARY_DIR}/icons/${icon}.rc")
    configure_file("${PROJECT_SOURCE_DIR}/assets/icons/Meow2.rc.in" "${script}" @ONLY)
    target_sources(${target} PRIVATE "${script}")
    set_source_files_properties("${script}" PROPERTIES OBJECT_DEPENDS "${MEOW2_ICON_FILE}") # a new icon rebuilds it
endfunction()

function(meow2_app_fonts target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${MEOW2_FONTS_DIR}" "$<TARGET_FILE_DIR:${target}>/fonts"
        VERBATIM)
    get_property(meow2FontsInstalled GLOBAL PROPERTY MEOW2_FONTS_INSTALLED)
    if(NOT meow2FontsInstalled)
        install(DIRECTORY "${MEOW2_FONTS_DIR}/" DESTINATION fonts) # next to the exes, like D3D12/
        set_property(GLOBAL PROPERTY MEOW2_FONTS_INSTALLED TRUE)
    endif()
endfunction()
