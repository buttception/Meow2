# Architecture: dependency injection, ownership, threading

Rules: [techstack.md §18–19](../../techstack.md#18-architecture-dependency-injection-ownership-no-globals). This page explains them with examples.

## The problem with globals

```cpp
// ❌ Anti-pattern — what Meow2 does NOT do
Logger& Logger::get();               // singleton
extern RhiDevice* g_device;          // global

void Renderer::renderFrame()
{
    Logger::get().info("frame");    // hidden dependency
    g_device->submit(...);           // hidden dependency
}
```

Reading `Renderer`'s constructor tells you nothing about what it needs. Hidden dependencies cause four problems:
- **You can't see the dependency graph.** "What breaks if I change the device?" means searching the whole codebase.
- **Initialisation order is implicit.** Who creates `g_device`, and when? Use it too early and you crash. Globals in different `.cpp` files are even initialised in an *unspecified* order (the "static initialisation order fiasco").
- **Testing is hard.** A test can't give `Renderer` a different logger or device; it has to set up the real globals.
- **Threading is dangerous.** Any thread can touch a global at any time. A data race on shared mutable state is invisible when that state isn't in anyone's constructor.

## Dependency injection

A class receives what it uses; it doesn't go and get it:

```cpp
#include <cstdio>
#include <string_view>

namespace Meow2
{
// Thread safety: thread-safe (stand-in for the spdlog-backed Logger).
class Logger
{
public:
    void info(std::string_view message)
    {
        std::printf("[info] %.*s\n", static_cast<int>(message.size()), message.data());
    }
};

// Thread safety: not thread-safe; main thread only.
class Window
{
public:
    explicit Window(Logger& logger)
        : m_logger(logger)
    {
        m_logger.info("window created");
    }

    bool isOpen() const
    {
        return m_isOpen;
    }

private:
    Logger& m_logger; // injected, not owned
    bool m_isOpen = true;
};

// Many dependencies? Group them so call sites stay readable.
// Plain data struct (no behaviour), so its fields don't use m_.
struct RendererDeps
{
    Logger& logger;
    Window& window;
};

// Thread safety: not thread-safe.
class Renderer
{
public:
    explicit Renderer(const RendererDeps& deps)
        : m_logger(deps.logger)
        , m_window(deps.window)
    {
    }

    void renderFrame()
    {
        if (m_window.isOpen())
        {
            m_logger.info("frame rendered");
        }
    }

private:
    Logger& m_logger;
    Window& m_window;
};
} // namespace Meow2

// Composition root: the only place that creates subsystems, in dependency order.
int main()
{
    Meow2::Logger logger;                                               // 1. no dependencies
    Meow2::Window window{logger};                                       // 2. needs Logger
    Meow2::Renderer renderer{{.logger = logger, .window = window}};     // 3. needs both

    renderer.renderFrame();
    return 0;
} // destroyed in reverse order: renderer, window, logger
```

That's the whole pattern:
1. **Constructor = dependency list.** `Renderer` needs a `Logger` and a `Window`, and you can read that in one line.
2. **References, not owners.** `Logger& m_logger;` says "I use it, someone else owns it". References can't be null and can't be re-pointed, so no `if (m_logger)` checks are needed.
3. **`<Name>Deps` struct** when the list gets long: designated initialisers (`.logger = …`) keep the call site readable. A `Deps` struct is plain data (no behaviour), so its fields have no `m_` prefix (techstack.md §16).
4. **One composition root.** `main()` (later an `Application` class) creates everything in dependency order and owns it. Nothing else calls `new Renderer`.

### Lifetimes

A dependency must **outlive** whatever references it. With locals that is automatic: C++ destroys locals in reverse order. With an `Application` class, **members are constructed in the order they are declared** (not the order in the initialiser list), so declare them in dependency order:

```cpp
class Application
{
    // Order matters: constructed top to bottom, destroyed bottom to top.
    Logger m_logger;
    Window m_window;     // uses m_logger
    Renderer m_renderer; // uses m_logger and m_window
};
```

### What about build-time backends?

The *type* of a subsystem is chosen by CMake (e.g. `PhysicsWorld` = Jolt), but the *instance* is still created by the composition root and injected. In unit tests, a test creates its own small set of real or null-backend objects and injects them. No global state needs resetting between tests.

### The few allowed globals

Marked `// GLOBAL: <reason>` and called out in PRs. Example: the Windows crash handler. `SetUnhandledExceptionFilter` takes a plain function with no "user data" pointer, so the handler needs one static pointer back to the `CrashHandler` object (set and cleared by the composition root).

## Threading-ready code

Today everything runs on the main thread; the job system arrives in M1. To make that switch painless, every class follows these rules now:

| Rule | Why |
|---|---|
| State thread safety in the header: *not thread-safe* / *immutable after construction* / *thread-safe (how)* | The caller knows whether it may share the object across jobs |
| No mutable globals or statics | Shared state is visible in constructors, so races are visible in review |
| Prefer immutable data → per-thread data → queues → locks | Each step down is more error-prone |
| Never hold a lock while calling into another subsystem or a callback | The callee might take another lock, which can deadlock |
| `std::jthread` + `std::stop_token` for threads, created only by the job system / composition root | Threads always shut down and are joined; no detached threads |
| Atomics with the default (sequentially consistent) ordering unless profiled | Weaker orderings are easy to get wrong |

Later, ThreadSanitizer presets (M1) run the tests and report data races automatically.
