#!/bin/sh
# Shared by the git hooks in this folder (post-merge, post-rewrite, post-checkout).
#
# Usage: regenerate-vs-solution.sh <old commit> <new commit>
#
# If a CMake input changed between the two commits and a Visual Studio solution exists in
# build/vs2022, re-run CMake through the regenerate-only preset (it compiles nothing). Visual
# Studio notices the rewritten .sln/.vcxproj files and offers to reload them.
#
# A hook must never break the git command that ran it, so this script always exits 0 and only
# prints what it did (or what to run by hand).

old="$1"
new="$2"
if [ -z "$old" ] || [ -z "$new" ] || [ "$old" = "$new" ]; then
    exit 0
fi

root=$(git rev-parse --show-toplevel 2>/dev/null) || exit 0
if [ ! -f "$root/build/vs2022/Meow2.sln" ]; then
    exit 0 # no Visual Studio solution generated in this checkout
fi

# Files CMake reads while configuring. A change to anything else (e.g. a .cpp) never changes the
# generated projects, because source files are listed explicitly in CMakeLists.txt.
changed=$(git diff --name-only "$old" "$new" -- 2>/dev/null |
    grep -E '(^|/)CMakeLists\.txt$|\.cmake$|\.in$|^CMakePresets\.json$')
if [ -z "$changed" ]; then
    exit 0
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "meow2: CMake files changed, but cmake is not on PATH. Run: cmake --build --preset windows-vs2022-regenerate"
    exit 0
fi

echo "meow2: CMake files changed; regenerating build/vs2022 (Visual Studio will offer to reload)..."
if (cd "$root" && cmake --build --preset windows-vs2022-regenerate >/dev/null 2>&1); then
    echo "meow2: solution regenerated."
else
    echo "meow2: regenerating failed. Run: cmake --preset windows-vs2022"
fi
exit 0
