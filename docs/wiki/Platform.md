# Platform & build configuration (`Meow2/Core/Platform.h`)

One header answers "what am I being compiled for?". Use it instead of raw compiler macros (`_WIN32`, `__clang__`, `NDEBUG`, …).

```cpp
#include <Meow2/Core/Platform.h>

#if MEOW2_PLATFORM_WINDOWS
    // Win32-only code (better: put it in a Win32/ source file, see techstack.md §1)
#endif

if constexpr (Meow2::c_isDebugBuild)
{
    validateEverything();
}
```

## Macros

All are **always defined as 0 or 1**. Test them with `#if`, never `#ifdef` (that would always be true).

| Group | Macros | Source |
|---|---|---|
| Platform | `MEOW2_PLATFORM_WINDOWS`, `MEOW2_PLATFORM_LINUX`, `MEOW2_PLATFORM_ANDROID` | compiler (`_WIN32`, `__linux__`, `__ANDROID__`) |
| Compiler | `MEOW2_COMPILER_MSVC`, `MEOW2_COMPILER_CLANG`, `MEOW2_COMPILER_GCC` | compiler (Clang checked first: clang-cl also defines `_MSC_VER`) |
| CPU | `MEOW2_ARCH_X64`, `MEOW2_ARCH_ARM64` | compiler |
| Configuration | `MEOW2_CONFIG_DEBUG`, `MEOW2_CONFIG_RELWITHDEBINFO`, `MEOW2_CONFIG_RELEASE` | **CMake** (`Meow2Engine/CMakeLists.txt`, PUBLIC on `Meow2::Engine`); MinSizeRel counts as Release |
| Helpers | `MEOW2_DEBUG_BREAK()`, `MEOW2_FORCE_INLINE` | |

Exactly one macro per group is 1; anything else is a compile-time `#error`. A misspelled macro name in `#if` is a warning, and so an error, on GCC/Clang (`-Wundef`).

## Constants

`Meow2::c_platformName`, `c_compilerName`, `c_configName` (strings for logs), `c_isDebugBuild` (for `if constexpr`). The apps print them at startup:

```
Meow2Renderer (Meow2 Engine 0.1.0, Linux/Clang/RelWithDebInfo)
```

## Notes

- If `CMAKE_BUILD_TYPE` is empty with Ninja/Makefiles, CMake now defaults it to `Debug` (presets always set it).
- Including the header without linking `Meow2::Engine` fails with `MEOW2_CONFIG_* are not defined by the build system`: the configuration macros come from the engine target.
