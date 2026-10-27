# Code style & warnings

## Formatting (clang-format)

The style lives in `.clang-format` at the repo root. Highlights:
- 4 spaces, no tabs, 120 columns
- Allman braces (opening brace on its own line), braces always present (`InsertBraces`)
- `int* ptr`, `int& ref` (pointer/reference bind to the type)
- no namespace indentation; `} // namespace Meow2` added automatically
- one statement per line: no one-line `if`s/loops/functions (except empty functions)
- includes grouped and sorted: own header → `"local.h"` → `<Meow2/...>` → third-party → standard library

```
cmake --build --preset windows-msvc-debug --target format        # fix all files
cmake --build --preset windows-msvc-debug --target format-check  # just check
ctest --preset windows-msvc-debug -L lint                        # Format.Check test
```

Editors: Visual Studio and VS Code (C/C++ extension) pick up `.clang-format` automatically. Turn on *format on save* to never think about it.

Needs **clang-format 19** (pinned). Different major versions format a few edge cases differently, so a range like "17 or newer" let `Format.Check` pass on one machine and fail on another. 19 is what Visual Studio 2022 ships (Individual components → *C++ Clang tools for Windows*), and CI installs `clang-format-19` on Linux.
CMake looks for `clang-format-19` or `clang-format` on `PATH` and inside Visual Studio 2022. With a missing or different version, the format targets and `Format.Check` are skipped locally, with a message at configure time. In CI (`CI` environment variable set) the configure step fails instead, so the check can't silently disappear.

To bump the version later: change `meow2ClangFormatRequiredMajor` in `cmake/Meow2Format.cmake` and the package in `.github/workflows/ci.yml`, run the `format` target, and commit the reformatted files in the same PR.

## EditorConfig (all text files)

`.editorconfig` gives every editor the same basics for **all** files, including CMake, JSON, YAML and Markdown that clang-format never touches:

| Setting | Value |
|---|---|
| charset | UTF-8 |
| line endings | LF (CRLF for `.bat/.cmd/.ps1/.sln/.vcxproj/.props`, matching `.gitattributes`) |
| indentation | 4 spaces (2 for YAML; tabs only in `.gitmodules`, which git writes itself) |
| final newline | always |
| trailing whitespace | trimmed |

Visual Studio, Rider and CLion support it natively. VS Code needs the *EditorConfig for VS Code* extension.

The test `Lint.EditorConfig.TextFilesFollowRules` (label `lint`) checks every text file tracked by git (outside `external/`) for a final newline, trailing whitespace, tab indentation and CR characters, and reports `file:line` for each problem.

## Warnings

Every Meow2 target calls `meow2_set_warnings(<target>)` (see `cmake/Meow2Warnings.cmake`):

| Compiler | Flags |
|---|---|
| MSVC | `/W4 /permissive- /utf-8` + extra level-1 warnings (unhandled enum in switch, lossy conversion, non-virtual destructor, …) |
| GCC / Clang | `-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Woverloaded-virtual -Wconversion -Wnull-dereference -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough` (+ GCC: `-Wduplicated-cond -Wduplicated-branches -Wlogical-op`) |

**Warnings are errors** (`/WX`, `-Werror`), controlled by `MEOW2_WARNINGS_AS_ERRORS` (default ON).
For a quick local experiment you can turn it off without editing anything:
```
cmake --preset windows-msvc-debug --compile-no-warning-as-error
```

Third-party code (googletest, later SDL3, Jolt, …) does **not** get these flags, and its headers are included as *system* headers, so its warnings never show up in our build.

The test `Build.Warnings.AreErrors` builds a file with a deliberate unused variable and passes only if that build **fails** with the warning. If someone ever disables warnings-as-errors by accident, this test catches it.
