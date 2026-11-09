# Third-party libraries from external/ (git submodules, techstack.md §4) used by the engine.
# GoogleTest is set up separately in Meow2Tests.cmake (tests only).
#
# Every library is added with EXCLUDE_FROM_ALL (only built when something links it) and SYSTEM
# (its headers are system headers, so our strict warnings never fire on its code). Their targets
# never get meow2_set_warnings(). They are linked PRIVATE: no third-party header appears in
# Meow2's public headers (AGENTS.md §3). The exceptions are tl::expected (Result<T> is part of
# the public API until C++23 std::expected), DirectXMath (Meow2/Math) and EnTT (its registry and
# views are the Scene API, techstack.md §25).

# meow2_require_submodule(<name> <dir>)
#   Stops with a helpful message when a submodule folder is empty (cloned without
#   --recurse-submodules).
function(meow2_require_submodule name dir)
    if(NOT EXISTS "${dir}/CMakeLists.txt")
        message(FATAL_ERROR
            "${name} is missing (${dir} is empty).\n"
            "Fetch the submodules with:\n"
            "  git submodule update --init --recursive")
    endif()
endfunction()

# ---- tl::expected (Result<T>, techstack.md §3) ---------------------------------------------------
# Header-only. Its own CMakeLists.txt includes CTest and enables its tests (downloading Catch2),
# which would add CDash targets to our build, so we define a small INTERFACE target over its
# include folder instead of add_subdirectory(). Include it only through <Meow2/Core/Result.h>.
set(meow2ExpectedDir "${PROJECT_SOURCE_DIR}/external/expected")
meow2_require_submodule(tl::expected "${meow2ExpectedDir}")

add_library(meow2_tl_expected INTERFACE)
target_include_directories(meow2_tl_expected SYSTEM INTERFACE "${meow2ExpectedDir}/include")
add_library(tl::expected ALIAS meow2_tl_expected)

# ---- spdlog (logging, techstack.md §7) ----------------------------------------------------------
set(meow2SpdlogDir "${PROJECT_SOURCE_DIR}/external/spdlog")
meow2_require_submodule(spdlog "${meow2SpdlogDir}")

set(SPDLOG_BUILD_SHARED OFF)            # static library, linked into Meow2Engine
set(SPDLOG_BUILD_EXAMPLE OFF)
set(SPDLOG_BUILD_TESTS OFF)
set(SPDLOG_BUILD_BENCH OFF)
set(SPDLOG_INSTALL OFF)
set(SPDLOG_DISABLE_DEFAULT_LOGGER ON)   # no global default logger: loggers are injected (§18)
set(SPDLOG_PREVENT_CHILD_FD ON)         # child processes (tools we launch, death tests) don't inherit log files
if(WIN32)
    # File names as wchar_t on Windows, so log folders with non-ASCII characters work
    # (the narrow Windows file APIs use the ANSI code page, not UTF-8).
    set(SPDLOG_WCHAR_FILENAMES ON)
endif()

add_subdirectory("${meow2SpdlogDir}" "${PROJECT_BINARY_DIR}/external/spdlog" EXCLUDE_FROM_ALL SYSTEM)
set_target_properties(spdlog PROPERTIES FOLDER "External/spdlog") # IDE solution folder

# ---- toml++ (config files, techstack.md §3) -----------------------------------------------------
# Header-only. As with tl::expected, its own CMakeLists.txt is skipped (it would add install and test
# targets); a small INTERFACE target over its include folder is enough. Only src/Core/Config/Config.cpp
# includes it. TOML_EXCEPTIONS=0: parse errors come back as a value we turn into Result<T>, not as a throw.
set(meow2TomlDir "${PROJECT_SOURCE_DIR}/external/tomlplusplus")
meow2_require_submodule(toml++ "${meow2TomlDir}")

add_library(meow2_tomlplusplus INTERFACE)
target_include_directories(meow2_tomlplusplus SYSTEM INTERFACE "${meow2TomlDir}/include")
target_compile_definitions(meow2_tomlplusplus INTERFACE TOML_EXCEPTIONS=0)
add_library(tomlplusplus::tomlplusplus ALIAS meow2_tomlplusplus)

# ---- DirectXMath (math, techstack.md §10) ---------------------------------------------------------
# Header-only; its own CMakeLists.txt is skipped (install and test targets). Linked PUBLIC by Meow2Engine:
# Meow2/Math exposes its types (the public-header exception). DirectXMath includes "sal.h", which only the
# Windows SDK has, so GCC/Clang get a shim that defines the annotations as nothing.
set(meow2DirectXMathDir "${PROJECT_SOURCE_DIR}/external/DirectXMath")
meow2_require_submodule(DirectXMath "${meow2DirectXMathDir}")

add_library(meow2_directxmath INTERFACE)
target_include_directories(meow2_directxmath SYSTEM INTERFACE "${meow2DirectXMathDir}/Inc")
if(NOT WIN32)
    target_include_directories(meow2_directxmath SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/Meow2Engine/shims")
endif()
add_library(DirectXMath::DirectXMath ALIAS meow2_directxmath)

# ---- EnTT (entities and components, techstack.md §25) --------------------------------------------
# Header-only. Its own CMakeLists.txt is skipped like the others (install, test and testbed targets); a
# small INTERFACE target over src/ is enough. Linked PUBLIC by Meow2Engine: Meow2/Scene/Scene.h exposes
# the registry and views (the owner-approved public-header exception).
set(meow2EnttDir "${PROJECT_SOURCE_DIR}/external/entt")
meow2_require_submodule(EnTT "${meow2EnttDir}")

add_library(meow2_entt INTERFACE)
target_include_directories(meow2_entt SYSTEM INTERFACE "${meow2EnttDir}/src")
add_library(EnTT::EnTT ALIAS meow2_entt)

# ---- SDL3 (window, input, gamepads; techstack.md §9) ---------------------------------------------
# Static library, linked into Meow2Engine; only src/Platform/**/Sdl/ include its headers. Subsystems the
# engine doesn't use are off: rendering goes through our own RHI (no SDL_Render/SDL_GPU), and audio and
# camera come back with their own features. Joystick/HIDAPI/haptic stay on for gamepads (M2 input).
set(meow2SdlDir "${PROJECT_SOURCE_DIR}/external/SDL")
meow2_require_submodule(SDL3 "${meow2SdlDir}")

set(SDL_SHARED OFF)
set(SDL_STATIC ON)
set(SDL_TEST_LIBRARY OFF)
set(SDL_TESTS OFF)
set(SDL_EXAMPLES OFF)
set(SDL_INSTALL OFF)
set(SDL_DISABLE_INSTALL ON)
set(SDL_AUDIO OFF)
set(SDL_CAMERA OFF)
set(SDL_GPU OFF)
set(SDL_RENDER OFF)
set(SDL_DIALOG OFF)
set(SDL_TRAY OFF)
set(SDL_OFFSCREEN ON) # the "offscreen" video driver: tests and smoke runs without a desktop (Linux CI)

add_subdirectory("${meow2SdlDir}" "${PROJECT_BINARY_DIR}/external/SDL" EXCLUDE_FROM_ALL SYSTEM)
foreach(meow2SdlTarget IN ITEMS SDL3-static SDL_uclibc)
    if(TARGET ${meow2SdlTarget})
        set_target_properties(${meow2SdlTarget} PROPERTIES FOLDER "External/SDL3") # IDE solution folder
    endif()
endforeach()
if(MSVC)
    # SDL compiles SDL_mslibc.c with /GL- (it must not be link-time optimised), which clashes with its precompiled
    # header in builds with LTO (warning C4652 in Release). Skipping the PCH for that file only removes the clash.
    set_source_files_properties("${meow2SdlDir}/src/stdlib/SDL_mslibc.c" DIRECTORY "${meow2SdlDir}"
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endif()

# ---- enkiTS (job system, techstack.md §19) -----------------------------------------------------------------------
# Static library; only src/Core/Jobs/Enkits/ includes it. The C interface, examples and install rules are off.
set(meow2EnkitsDir "${PROJECT_SOURCE_DIR}/external/enkiTS")
meow2_require_submodule(enkiTS "${meow2EnkitsDir}")

set(ENKITS_BUILD_C_INTERFACE OFF)
set(ENKITS_BUILD_EXAMPLES OFF)
set(ENKITS_BUILD_SHARED OFF)
set(ENKITS_INSTALL OFF)
add_subdirectory("${meow2EnkitsDir}" "${PROJECT_BINARY_DIR}/external/enkiTS" EXCLUDE_FROM_ALL SYSTEM)
set_target_properties(enkiTS PROPERTIES FOLDER "External/enkiTS")

# ---- Tracy client (profiler, techstack.md §3/§4) ------------------------------------------------------------------
# Static client library; only src/Core/Profiling/Tracy/ includes it. Profiling is on in Debug and RelWithDebInfo and
# compiled out of Release (and of sanitizer builds via MEOW2_PROFILING=OFF): TRACY_ENABLE is added per configuration
# below instead of with Tracy's own (configuration-wide) option.
#   - TRACY_ON_DEMAND: nothing is recorded until a viewer connects, so an unprofiled run costs next to nothing.
#   - TRACY_ONLY_LOCALHOST + TRACY_NO_BROADCAST: the client listens on 127.0.0.1 only and never announces itself on
#     the network.
#   - TRACY_NO_CRASH_HANDLER: our CrashHandler owns the crash hooks (techstack.md §8).
option(MEOW2_PROFILING "Tracy profiling in Debug and RelWithDebInfo builds" ON)
set(meow2TracyDir "${PROJECT_SOURCE_DIR}/external/tracy")
meow2_require_submodule(Tracy "${meow2TracyDir}")

set(TRACY_STATIC ON)
set(TRACY_ENABLE OFF)
set(TRACY_ON_DEMAND ON)
set(TRACY_ONLY_LOCALHOST ON)
set(TRACY_NO_BROADCAST ON)
set(TRACY_NO_CRASH_HANDLER ON)
add_subdirectory("${meow2TracyDir}" "${PROJECT_BINARY_DIR}/external/tracy" EXCLUDE_FROM_ALL SYSTEM)
set(meow2ProfilingEnabled "$<AND:$<BOOL:${MEOW2_PROFILING}>,$<NOT:$<CONFIG:Release,MinSizeRel>>>")
target_compile_definitions(TracyClient PUBLIC "$<${meow2ProfilingEnabled}:TRACY_ENABLE>")
set_target_properties(TracyClient PROPERTIES FOLDER "External/Tracy")

# ---- glTF and images (cgltf, stb_image; techstack.md §4, §13) ---------------------------------------
# Both are single headers whose implementation is compiled once, in its own library, so the engine's warnings don't
# apply to it. Only src/Assets/Gltf/ and src/Assets/Texture/ include them. Their memory goes through the engine's
# allocator hooks (MemTag::Assets): cgltf's through its options, stb_image's through the meow2Stb* functions the
# engine defines.
set(meow2CgltfDir "${PROJECT_SOURCE_DIR}/external/cgltf")
if(NOT EXISTS "${meow2CgltfDir}/cgltf.h")
    message(FATAL_ERROR "cgltf is missing (${meow2CgltfDir} is empty).\n"
                        "Fetch the submodules with:\n  git submodule update --init --recursive")
endif()
add_library(meow2_cgltf STATIC "${PROJECT_SOURCE_DIR}/Meow2Engine/src/Assets/Gltf/Cgltf.cpp")
target_include_directories(meow2_cgltf SYSTEM PUBLIC "${meow2CgltfDir}")
if(MSVC)
    target_compile_definitions(meow2_cgltf PRIVATE _CRT_SECURE_NO_WARNINGS) # fopen and strncpy in its file loader
endif()
set_target_properties(meow2_cgltf PROPERTIES FOLDER "External")

add_library(meow2_stb_image STATIC "${PROJECT_SOURCE_DIR}/Meow2Engine/src/Assets/Texture/StbImage.cpp")
target_include_directories(meow2_stb_image SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/external/stb")
set_target_properties(meow2_stb_image PROPERTIES FOLDER "External")

# ---- Dear ImGui (Meow2Renderer's UI; techstack.md §2, §4) ------------------------------------------
# Built only with the Renderer, which is its one user; never part of the engine or the player. Its own backends aren't
# compiled: Meow2Renderer draws it through the RHI and feeds it engine input events.
if(MEOW2_BUILD_RENDERER)
    set(meow2ImguiDir "${PROJECT_SOURCE_DIR}/external/imgui")
    if(NOT EXISTS "${meow2ImguiDir}/imgui.h")
        message(FATAL_ERROR "Dear ImGui is missing (${meow2ImguiDir} is empty).\n"
                            "Fetch the submodules with:\n  git submodule update --init --recursive")
    endif()
    add_library(meow2_imgui STATIC
        "${meow2ImguiDir}/imgui.cpp" "${meow2ImguiDir}/imgui_demo.cpp" "${meow2ImguiDir}/imgui_draw.cpp"
        "${meow2ImguiDir}/imgui_tables.cpp" "${meow2ImguiDir}/imgui_widgets.cpp")
    target_include_directories(meow2_imgui SYSTEM PUBLIC "${meow2ImguiDir}")
    # Only today's API: renamed and removed functions don't compile, so upgrades show what changed.
    target_compile_definitions(meow2_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
    set_target_properties(meow2_imgui PROPERTIES FOLDER "External")
endif()

# ---- Vulkan (headers, volk, VMA; techstack.md §4, §11) --------------------------------------------
# The app never links the Vulkan loader: volk finds it at runtime, so a machine without Vulkan gets a clear error
# (and D3D12 can still run). Only src/Graphics/Rhi/Vulkan/ includes these headers.
if(MEOW2_RHI_VULKAN)
    enable_language(C) # volk.c
    set(meow2VulkanHeadersDir "${PROJECT_SOURCE_DIR}/external/Vulkan-Headers")
    set(meow2VolkDir "${PROJECT_SOURCE_DIR}/external/volk")
    set(meow2VmaDir "${PROJECT_SOURCE_DIR}/external/VulkanMemoryAllocator")
    meow2_require_submodule(Vulkan-Headers "${meow2VulkanHeadersDir}")
    meow2_require_submodule(volk "${meow2VolkDir}")
    meow2_require_submodule(VulkanMemoryAllocator "${meow2VmaDir}")

    add_library(meow2_volk STATIC "${meow2VolkDir}/volk.c")
    target_include_directories(meow2_volk SYSTEM PUBLIC "${meow2VulkanHeadersDir}/include" "${meow2VolkDir}")
    target_compile_definitions(meow2_volk PUBLIC VK_NO_PROTOTYPES)
    if(WIN32)
        target_compile_definitions(meow2_volk PUBLIC VK_USE_PLATFORM_WIN32_KHR)
    else()
        target_compile_definitions(meow2_volk PUBLIC VK_USE_PLATFORM_XLIB_KHR VK_USE_PLATFORM_WAYLAND_KHR)
    endif()
    target_link_libraries(meow2_volk PUBLIC ${CMAKE_DL_LIBS})
    set_target_properties(meow2_volk PROPERTIES FOLDER "External")

    # VMA's implementation, in its own library so the engine's warnings don't apply to it.
    add_library(meow2_vma STATIC "${PROJECT_SOURCE_DIR}/Meow2Engine/src/Graphics/Rhi/Vulkan/VulkanMemoryAllocator.cpp")
    target_include_directories(meow2_vma SYSTEM PUBLIC "${meow2VmaDir}/include")
    target_link_libraries(meow2_vma PUBLIC meow2_volk)
    if(WIN32)
        target_compile_definitions(meow2_vma PUBLIC NOMINMAX) # VMA includes <windows.h>
    endif()
    set_target_properties(meow2_vma PROPERTIES FOLDER "External")
endif()

# ---- D3D12 (DirectX-Headers, D3D12MA; techstack.md §4, §11) -----------------------------------------
# The headers match the Agility SDK (cmake/Meow2Agility.cmake) version for version, so the code sees the same API as
# the runtime it loads. They come before the Windows SDK's own d3d12.h on the include path. Only
# src/Graphics/Rhi/D3D12/ includes them.
if(MEOW2_RHI_D3D12)
    set(meow2DirectXHeadersDir "${PROJECT_SOURCE_DIR}/external/DirectX-Headers")
    set(meow2D3d12maDir "${PROJECT_SOURCE_DIR}/external/D3D12MemoryAllocator")
    meow2_require_submodule(DirectX-Headers "${meow2DirectXHeadersDir}")
    meow2_require_submodule(D3D12MemoryAllocator "${meow2D3d12maDir}")

    add_library(meow2_directx_headers INTERFACE)
    target_include_directories(meow2_directx_headers SYSTEM BEFORE INTERFACE
        "${meow2DirectXHeadersDir}/include/directx" "${meow2DirectXHeadersDir}/include")
    target_compile_definitions(meow2_directx_headers INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(meow2_directx_headers INTERFACE d3d12 dxgi dxguid)

    # D3D12MA's implementation, in its own library so the engine's warnings don't apply to it.
    add_library(meow2_d3d12ma STATIC "${meow2D3d12maDir}/src/D3D12MemAlloc.cpp")
    target_include_directories(meow2_d3d12ma SYSTEM PUBLIC "${meow2D3d12maDir}/include")
    target_compile_definitions(meow2_d3d12ma PUBLIC D3D12MA_USING_DIRECTX_HEADERS)
    target_link_libraries(meow2_d3d12ma PUBLIC meow2_directx_headers)
    set_target_properties(meow2_d3d12ma PROPERTIES FOLDER "External")
endif()

# ---- DirectXTex (texture cooking: BC7 and BC5 compression; techstack.md §4, §13a) ----------------------------------
# Microsoft's texture library, compiled here from its sources rather than through its CMakeLists.txt: that one always
# adds install rules (its library, headers and CMake package would land in our release zip) and exports its target,
# which then can't link to ours. Its tools, sample and GPU (D3D11) compression aren't built, and it doesn't use
# OpenMP: the cooker spreads work over the job system instead. Only src/Assets/Texture/ includes it.
set(meow2DirectXTexDir "${PROJECT_SOURCE_DIR}/external/DirectXTex")
meow2_require_submodule(DirectXTex "${meow2DirectXTexDir}")
set(meow2DirectXTexSources
    BC.cpp BC4BC5.cpp BC6HBC7.cpp DirectXTexCompress.cpp DirectXTexConvert.cpp DirectXTexDDS.cpp DirectXTexHDR.cpp
    DirectXTexImage.cpp DirectXTexMipmaps.cpp DirectXTexMisc.cpp DirectXTexNormalMaps.cpp DirectXTexPMAlpha.cpp
    DirectXTexResize.cpp DirectXTexTGA.cpp DirectXTexUtil.cpp)
if(WIN32)
    list(APPEND meow2DirectXTexSources DirectXTexFlipRotate.cpp DirectXTexWIC.cpp) # as its own build does
endif()
list(TRANSFORM meow2DirectXTexSources PREPEND "${meow2DirectXTexDir}/DirectXTex/")
add_library(meow2_directxtex STATIC ${meow2DirectXTexSources})
target_include_directories(meow2_directxtex SYSTEM PUBLIC "${meow2DirectXTexDir}/DirectXTex")
target_link_libraries(meow2_directxtex PUBLIC meow2_directxmath)
if(WIN32)
    target_link_libraries(meow2_directxtex PUBLIC ole32) # CoCreateInstance, for WIC
else()
    # dxgiformat.h and the Windows types it needs, from DirectX-Headers' stubs for non-Windows builds.
    set(meow2DirectXHeadersDir "${PROJECT_SOURCE_DIR}/external/DirectX-Headers")
    meow2_require_submodule(DirectX-Headers "${meow2DirectXHeadersDir}")
    target_include_directories(meow2_directxtex SYSTEM PUBLIC
        "${meow2DirectXHeadersDir}/include" "${meow2DirectXHeadersDir}/include/wsl/stubs")
    target_compile_definitions(meow2_directxtex PUBLIC USING_DIRECTX_HEADERS)
    # DirectXTex's sources include DirectX-Headers' d3d12.h before anything includes sal.h, and the WSL stubs only
    # define some of its annotations; on Windows the SDK's headers bring sal.h in first. So our shim (all the
    # annotations these libraries use, as nothing) is included ahead of every DirectXTex source.
    target_compile_options(meow2_directxtex PRIVATE -include "${PROJECT_SOURCE_DIR}/Meow2Engine/shims/sal.h")
endif()
if(MEOW2_SANITIZER STREQUAL "address")
    # DirectXTex's BC7 encoder (BC6HBC7.cpp, ComputeError) reads its byte-aligned LDRColorA through DirectXMath's
    # 4-byte-aligned XMUBYTE4, which UBSan reports as a misaligned access. x64 and arm64 load unaligned words
    # without faulting, so it's harmless there; it's in a library we don't edit, so only that one check is turned off,
    # and only for this library. Every other sanitizer check still runs on it, and the engine keeps them all.
    target_compile_options(meow2_directxtex PRIVATE -fno-sanitize=alignment)
endif()
set_target_properties(meow2_directxtex PROPERTIES FOLDER "External")
add_library(DirectXTex::DirectXTex ALIAS meow2_directxtex)
