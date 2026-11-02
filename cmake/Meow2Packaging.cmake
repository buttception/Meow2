# Release packaging (techstack.md §21): install rules + CPack ZIP, `Meow2-<version>-windows-x64.zip`.
#
#   cmake --build --preset windows-msvc-release --target package
#
# The zip has one top-level folder with everything next to the executables, so it runs after unpacking anywhere:
#   - Meow2Player.exe, Meow2Renderer.exe, Meow2Editor.exe (if Qt was found) and their PDBs, so crash dumps (.dmp)
#     from that build can be read;
#   - the MSVC runtime DLLs (app-local: no "Visual C++ Redistributable" installer needed);
#   - Qt's DLLs and plugins for the editor (Meow2Editor/CMakeLists.txt runs Qt's deploy script);
#   - README.md, LICENSE, CHANGELOG.md.
# cmake/TestPackage.cmake checks a zip by unpacking it into an empty folder and running each app there.
#
# Included before the apps (they call meow2_install_app()); the top-level CMakeLists.txt calls meow2_package() at the
# end. Install rules run in the order they're declared (policy CMP0082), so every app is installed before the editor's
# Qt deploy step and before the runtime DLLs.

include(GNUInstallDirs)

# Everything in the archive's top folder, not in bin/: the apps find their DLLs (and Qt its qt.conf) next to them.
set(MEOW2_INSTALL_DIR ".")

# meow2_install_app(<target>)
#   Installs an app and (MSVC) its PDB into the package's top folder.
function(meow2_install_app target)
    install(TARGETS ${target} RUNTIME DESTINATION ${MEOW2_INSTALL_DIR})
    if(MSVC)
        install(FILES "$<TARGET_PDB_FILE:${target}>" DESTINATION ${MEOW2_INSTALL_DIR} OPTIONAL)
    endif()
endfunction()

# meow2_package()
#   The MSVC runtime, the documents and the CPack settings. Called once, at the end of the top-level CMakeLists.txt.
macro(meow2_package)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${MEOW2_INSTALL_DIR})
    set(CMAKE_INSTALL_UCRT_LIBRARIES ON) # the Universal CRT too, for Windows 10 PCs without its latest update
    include(InstallRequiredSystemLibraries)

    install(FILES "${PROJECT_SOURCE_DIR}/README.md" "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/CHANGELOG.md"
            DESTINATION ${MEOW2_INSTALL_DIR})

    if(WIN32)
        set(meow2PackagePlatform "windows-x64")
    else()
        string(TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" meow2PackagePlatform)
    endif()
    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_NAME "Meow2")
    set(CPACK_PACKAGE_VENDOR "Meow2")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    # The plain version (x.y.z) in the file name: release zips are built from the tagged commit. The executables
    # inside still report the commit (getVersionString(), techstack.md §21).
    set(CPACK_PACKAGE_FILE_NAME "Meow2-${PROJECT_VERSION}-${meow2PackagePlatform}")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
    set(CPACK_PACKAGE_DIRECTORY "${PROJECT_BINARY_DIR}/package")
    set(CPACK_VERBATIM_VARIABLES ON)
    include(CPack)
endmacro()
