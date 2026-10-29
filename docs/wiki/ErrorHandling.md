# Error handling: `Result<T>`

`#include <Meow2/Core/Result.h>`

Meow2 has two kinds of "something went wrong", and they're handled differently:

| Kind | Example | Tool |
|---|---|---|
| **A bug**: the code is wrong | index out of range, null where there can't be one | `MEOW2_ASSERT` ([Asserts](Asserts.md)) |
| **An expected failure**: the code is right, the world isn't | file missing, bad config value, unsupported format, disk full | **`Result<T>`** (this page) |

## Result in one minute

A `Result<T>` holds **either** a `T` (success) **or** an `Error` (failure), never both:

```cpp
Meow2::Result<Config> loadConfig(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path))
    {
        return Meow2::fail(Meow2::ErrorCode::NotFound, path.string());   // failure
    }
    return parse(path);                                                  // success: a Config converts implicitly
}

const Meow2::Result<Config> config = loadConfig("config.toml");
if (config)                                  // or config.has_value()
{
    apply(*config);                          // the value (also config->field)
}
else
{
    MEOW2_LOG_WARN(m_log, "Using defaults ({})", config.error());     // "NotFound: config.toml"
}
```

- **`Result<void>`** is for operations that return nothing on success: `return {};` for success, `return fail(...)` for failure.
- **`Error`** is plain data: `ErrorCode code` (to branch on) + `std::string message` (for people). It formats as `Code: message`, so it can go straight into log calls.
- **`ErrorCode`** values: `Unknown`, `InvalidArgument`, `NotFound`, `AlreadyExists`, `AccessDenied`, `IoError`, `ParseError`, `Unsupported`, `OutOfMemory`, `Timeout`, `Cancelled`. `toString(code)` gives the name.
- A module that needs richer error data can use `Result<T, MyError>`.

## Chaining

Instead of nested `if`s, steps can be chained. Each one runs only if the previous one succeeded:

```cpp
const Meow2::Result<Texture> texture =
    readFile(path)                                       // Result<std::vector<std::byte>>
        .and_then(decodePng)                             // runs if reading worked; returns Result<Image>
        .transform([](Image image) { return upload(image); })   // changes the value, can't fail
        .or_else([&](const Meow2::Error& error) -> Meow2::Result<Texture> {
            MEOW2_LOG_WARN(m_log, "{}: using the fallback texture", error);
            return m_fallbackTexture;                    // recover
        });
```

| Call | Runs when | Returns |
|---|---|---|
| `and_then(f)` | success | `f(value)`, which returns a `Result` itself |
| `transform(f)` | success | `Result` of `f(value)` |
| `or_else(f)` | failure | `f(error)`, which returns a `Result` (to recover) |
| `transform_error(f)` | failure | the same `Result` with `f(error)` as the error |
| `value_or(x)` | always | the value, or `x` on failure |

## Rules

- **Only use what `std::expected` (C++23) also has.** That's the table above, plus `has_value`, `operator bool`, `*`, `->`, `value()` and `error()`. tl::expected has extra names such as `map` and `map_error`; don't use them. The day Meow2 moves to C++23, `Result` becomes `std::expected` by changing one line.
- **Only return what `fail()` gives you; don't read it.** Its accessor is `value()` in tl but `error()` in `std`. Read the error from the `Result` (`result.error()`).
- **Check before you dereference.** `*result` on a failure fails a Meow2 assert in Debug/RelWithDebInfo, and is undefined in Release. `value()` throws `bad_expected_access`; prefer checking first.
- **Include `<Meow2/Core/Result.h>`, never `<tl/expected.hpp>`.** Result.h routes tl's internal checks to `MEOW2_ASSERT`. Including tl first is a compile error, on purpose.
- **No exceptions for expected failures.** Exceptions stay for truly exceptional situations, and engine APIs report expected failures through `Result`.

## Why tl::expected

`std::expected` is the standard type, but it's C++23, and Meow2 is C++20. [tl::expected](https://github.com/TartanLlama/expected) (v1.3.1, CC0, header-only) is the most widely used implementation of the same design for older standards, with the same member names for the C++23 subset.

It's the one third-party library whose header appears in Meow2's public headers. `Result` is part of the API itself, so it can't be hidden behind a pimpl. This exception is recorded in AGENTS.md §3 and techstack.md §4.

## Tests

`Meow2EngineCoreTests` → `Core.Result.*`:
- values and errors;
- `Result<void>`;
- the std-subset chaining operations;
- error formatting and code names;
- a death test showing that `*result` on an error fails a Meow2 assert.
