# Testing

All tests run through **CTest**, so one command runs everything:

```
ctest --preset windows-msvc-debug          # all tests
ctest --preset windows-msvc-debug -L unit  # only unit tests
ctest --preset windows-msvc-debug -R Core  # tests whose name matches "Core"
ctest --preset windows-msvc-debug --rerun-failed
```

## Kinds of tests

| Label | What | How it's made |
|---|---|---|
| `unit` | Tests one class/function in isolation, in milliseconds | GoogleTest, `meow2_add_test_suite()` |
| `smoke` | Runs an executable (with optional `ARGS`) and checks it prints the expected text and exits with 0. Apps with a window run with `--set platform.videoDriver=offscreen --set app.quitAfterFrames=3`, so no desktop is needed | `meow2_add_smoke_test()` |
| `integration` | Several modules together / CLI / build behaviour (e.g. `Build.Warnings.AreErrors`) | GoogleTest or `add_test()` |
| `lint` | Source checks: `Format.Check`, `Lint.EditorConfig.TextFilesFollowRules`, `Docs.PublicApi.IsDocumented` | `add_test()` |
| `gpu`, `visual`, `windows` | Need a GPU, golden images, or Windows | later milestones |

## Test names

`<Module>.<Unit>.<Behaviour>`, e.g. `Core.Version.MatchesProjectVersion`.
- *Module* comes from the `MODULE` argument of `meow2_add_test_suite()`.
- *Unit* and *Behaviour* are the two names in `TEST(Unit, Behaviour)`.

## Adding unit tests

1. Put the test file next to the module's other tests: `Meow2Engine/tests/<Subsystem>/[<Module>/]<Thing>Tests.cpp`.
2. Write tests:
   ```cpp
   #include <Meow2/Core/Version.h>
   #include <gtest/gtest.h>

   TEST(Version, StringIsMajorMinorPatch)
   {
       EXPECT_EQ(std::string(Meow2::getVersionString()), "0.1.0");
   }
   ```
   `EXPECT_*` records a failure and continues; `ASSERT_*` stops the test (use it when continuing makes no sense, e.g. a null pointer).
3. Register the file in `Meow2Engine/tests/CMakeLists.txt`:
   ```cmake
   meow2_add_test_suite(
       TARGET  Meow2EngineCoreTests   # one executable per module
       MODULE  Core
       SOURCES Core/VersionTests.cpp Core/HandleTests.cpp
       LABELS  unit)                  # optional, default: unit
   ```
4. Build and run `ctest --preset … -R Core`.

## Sanitizers

Two CI jobs run **every test** with compiler instrumentation that checks the program while it runs (techstack.md §19):

| Job | Preset | Catches |
|---|---|---|
| `Linux (Clang ASan+UBSan)` | `linux-clang-asan` | **AddressSanitizer**: out-of-bounds reads/writes, use-after-free, double free, stack-use-after-return, and leaks at exit (LeakSanitizer). **UndefinedBehaviorSanitizer**: signed overflow, misaligned or null access, invalid casts and shifts. |
| `Linux (Clang TSan)` | `linux-clang-tsan` | **ThreadSanitizer**: data races (two threads touching the same memory without synchronisation) and lock-order problems. |

- **Any report fails the test.** The test presets set `ASAN_OPTIONS=…:abort_on_error=1`, `UBSAN_OPTIONS=…:halt_on_error=1` and `TSAN_OPTIONS=halt_on_error=1`, and the code is built with `-fno-sanitize-recover`.
- **Third-party libraries are instrumented too** (SDL, enkiTS, GoogleTest). `cmake/Meow2Sanitizers.cmake` adds the flags before they're added. TSan only understands synchronisation in code it instrumented, so an uninstrumented library would show up as false races.
- **Suppressions:** `cmake/TsanSuppressions.txt` lists reports from uninstrumented system libraries only (today: lock-order reports inside `libdbus`, which SDL loads during `SDL_Init`/`SDL_Quit`). Never add Meow2 or `external/` code there; fix the race instead.
- **What's switched off in these builds:**
  - **memory tracking** (`MEOW2_MEMORY_TRACKING=0`): the sanitizers replace `new`/`delete` themselves, so the tracking tests are skipped and the `NoLeaks` smoke tests only check that the app runs; LeakSanitizer does the leak check;
  - **profiling** (`MEOW2_PROFILING=OFF`): Tracy would only add noise;
  - **the editor:** Qt isn't built with sanitizers, so only the engine and Player/Renderer are checked. The editor's own code isn't. Revisit when the MCP server adds a network thread (M9).
- **Running them yourself** needs Linux (or WSL) with Clang: `cmake --workflow --preset linux-clang-asan`, or `linux-clang-tsan`. They're 2–15× slower than a normal Debug run.
- **Reading a report:** the first stack in the output is where the bad access happened; "previous write"/"freed by" stacks show the other side. Fix the code, never the sanitizer options.

## How it works

- GoogleTest is a git submodule in `external/googletest` (v1.17.0). Clone with `--recurse-submodules`, or run `git submodule update --init --recursive`. Configure stops with that hint if it is missing.
- `GTest::gtest_main` provides `main()`, so test files contain only `TEST()`s.
- `gtest_discover_tests` asks the test executable for its list of tests (when `ctest` runs) and registers each one as its own CTest test, so they can be filtered, labelled and re-run individually.
- `-DMEOW2_BUILD_TESTS=OFF` skips GoogleTest and all unit tests (smoke tests stay).

## Implementation summary

- `MEOW2_SANITIZER=address|thread` (`cmake/Meow2Sanitizers.cmake`, flags for every target incl. third-party, memory tracking off), presets `linux-clang-asan` and `linux-clang-tsan` (no editor, no profiling), CI jobs `Linux (Clang ASan+UBSan)` and `Linux (Clang TSan)`.
