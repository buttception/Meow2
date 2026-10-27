# Asserts

`#include <Meow2/Core/Assert.h>`: checks for conditions that can only be false because of a bug.

## Which macro

| Macro | Debug | RelWithDebInfo | Release | Use for |
|---|---|---|---|---|
| `MEOW2_ASSERT(cond)` | checked | checked | **removed**: `cond` isn't evaluated | invariants, preconditions, "can't happen" |
| `MEOW2_ASSERT_SLOW(cond)` | checked | removed | removed | expensive checks: walking a container, validating a whole structure |
| `MEOW2_VERIFY(cond)` | checked | checked | `cond` **runs**, result ignored | calls whose side effect is needed in every build |

```cpp
MEOW2_ASSERT(index < m_count);
MEOW2_ASSERT(index < m_count, "index out of range");     // optional message: a string literal
MEOW2_ASSERT_SLOW(isSorted(m_items), "m_items must stay sorted");
MEOW2_VERIFY(m_file.close());                           // close() runs in Release too
```

- **Don't put side effects in `MEOW2_ASSERT`.** `MEOW2_ASSERT(queue.pop())` would stop popping in Release. Use `MEOW2_VERIFY` for that.
- **Asserts are not error handling.** A missing file, a bad config value or a lost network connection can happen in a correct program, so return a `Result` and handle it. An assert says "if this is false, the code is wrong".
- **The message must be a string literal.** `MEOW2_ASSERT(p, name)` with a variable doesn't compile, on purpose. Formatted messages arrive with logging.
- A disabled assert still **compiles** its condition (inside `sizeof`), so a renamed variable is still an error and its variables don't trigger "unused" warnings.
- `MEOW2_ASSERTS_ENABLED` / `MEOW2_SLOW_ASSERTS_ENABLED` can be forced with a compile definition, e.g. asserts in a Release build used for testing. In code, use `Meow2::c_assertsEnabled` / `c_slowAssertsEnabled` with `if constexpr`.

## What happens on failure

```
MEOW2_ASSERT(index < m_count, "index out of range")   fails
        │
        ▼
Detail::reportAssertFailure(AssertInfo{expression, message, file, line, function})
        │
        ▼
current AssertHandler (default: defaultAssertHandler)
        │  returns AssertAction
        ├─ Break    → the macro hits MEOW2_DEBUG_BREAK() on the assert's own line
        ├─ Continue → execution goes on (tests, "report once" handlers)
        └─ Abort    → std::abort()
```

The **default handler** prints this to `stderr`:
```
Assertion failed: index < m_count
  Message: index out of range
  At: F:/Meow2/Meow2Engine/src/Foo.cpp(42) in getItem
```
Then it stops in the debugger if one is attached (`isDebuggerAttached()`), and aborts otherwise. `file(line)` is the format Visual Studio's output window makes clickable.

The break happens **in the macro, at the assert's line**, not inside the handler. So the debugger shows your code with all its local variables, not engine internals.

## Custom handlers

```cpp
Meow2::AssertAction myHandler(const Meow2::AssertInfo& info)
{
    // log it, show a dialog, count it, ...
    return Meow2::AssertAction::Break;
}

const Meow2::AssertHandler previous = Meow2::setAssertHandler(&myHandler);   // composition root
// ...
Meow2::setAssertHandler(previous);    // nullptr also restores the default
```

- The handler is a **plain function pointer**: it can be called on a corrupted heap or out of memory without allocating.
- It's called **on the failing thread** and must be thread-safe. `setAssertHandler` itself is thread-safe (atomic).
- An assert that fails **inside** a handler aborts immediately, instead of recursing until the stack overflows.
- Later in M1, logging installs a handler that writes the failure to the log (and `fatal` flushes it), and the crash handler writes a `.dmp` on abort.

## Debugger detection

`#include <Meow2/Core/Debugger.h>`: `Meow2::isDebuggerAttached()` is `IsDebuggerPresent()` on Windows, and `TracerPid` from `/proc/self/status` on Linux/Android. Don't cache the result: a debugger can attach at any time.

## Tests

`Meow2EngineCoreTests`: `Core.Assert.*`, `Core.AssertWithHandler.*` and `Core.Verify.*`.
- **Death tests** (`EXPECT_DEATH`) run the failing assert in a child process and check that it dies with the expected text on stderr.
- **Recording-handler tests** check every `AssertInfo` field and that `Continue` resumes.
- In Release, the tests that need asserts enabled report **Skipped**.
