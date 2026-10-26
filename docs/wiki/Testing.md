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
| `smoke` | Runs an executable and checks it starts, prints the expected text and exits with 0 | `meow2_add_smoke_test()` |
| `integration` | Several modules together / CLI / build behaviour (e.g. `Build.Warnings.AreErrors`) | GoogleTest or `add_test()` |
| `lint` | Source checks: `Format.Check`, `Lint.EditorConfig.TextFilesFollowRules` | `add_test()` |
| `gpu`, `visual`, `windows` | Need a GPU, golden images, or Windows | later milestones |

## Test names

`<Module>.<Unit>.<Behaviour>`, e.g. `Core.Version.MatchesProjectVersion`.
- *Module* comes from the `MODULE` argument of `meow2_add_test_suite()`.
- *Unit* and *Behaviour* are the two names in `TEST(Unit, Behaviour)`.

## Adding unit tests

1. Put the test file next to the module's other tests: `Meow2Engine/tests/<Module>/<Thing>Tests.cpp`.
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

## How it works

- GoogleTest is a git submodule in `external/googletest` (v1.17.0). Clone with `--recurse-submodules`, or run `git submodule update --init --recursive`. Configure stops with that hint if it is missing.
- `GTest::gtest_main` provides `main()`, so test files contain only `TEST()`s.
- `gtest_discover_tests` asks the test executable for its list of tests (when `ctest` runs) and registers each one as its own CTest test, so they can be filtered, labelled and re-run individually.
- `-DMEOW2_BUILD_TESTS=OFF` skips GoogleTest and all unit tests (smoke tests stay).
