# Events

The `EventBus` delivers things that *happen*: a window resized, a key pressed, an asset loaded. Code that cares subscribes to a type, code that notices it publishes one, and neither knows about the other.

- Header: `#include <Meow2/Core/Events/EventBus.h>`, namespace `Meow2::Events`.
- Status: ✅ M1 (#66). The window and input events that use it come with M2.

## Using it

```cpp
struct WindowResized { std::uint32_t width; std::uint32_t height; }; // an event: plain data, no base class

Meow2::Events::EventBus events{Meow2::Memory::globalTracker()};      // composition root; injected where needed

class Swapchain
{
    // ...
    Meow2::Events::Subscription m_onResize; // declared LAST: destroyed first, before what the handler uses
};
m_onResize = events.subscribe<WindowResized>([this](const WindowResized& e) { resize(e.width, e.height); });

events.publish(WindowResized{1600, 900}); // main thread: handlers run now
events.enqueue(WindowResized{1600, 900}); // any thread: handlers run at the next dispatchQueued()
events.dispatchQueued();                  // main thread, at fixed points in the frame
```

| API | What it does |
|---|---|
| `subscribe<T>(handler)` | Calls `handler(const T&)` for every `T` from now on. Returns a `Subscription`. |
| `Subscription` | Move-only. Destroying it, or calling `reset()`, unsubscribes. |
| `publish(event)` | Calls the handlers of that type right away, in subscription order. Main thread only. |
| `enqueue(event)` | Moves the event into the queue. Thread-safe. |
| `dispatchQueued()` | Delivers everything queued so far, in order, then frees the queue. Main thread only. |

## Rules

- **Events are plain structs.** The type is the channel. There's no event base class, no listener interface and no RTTI: `eventTypeId<T>()` is the address of a variable that exists once per type, known at compile time.
- **Queued events must be movable and must not throw when destroyed.** They're moved into the queue and destroyed after delivery. Strings and vectors inside them are fine.
- **Changing things from inside a handler is safe:**
  - a subscription made in a handler gets events from the *next* delivery on;
  - a subscription reset in a handler isn't called again, not even later in the same delivery;
  - an event enqueued in a handler arrives at the *next* `dispatchQueued()`, so a handler can't start an endless loop;
  - a handler may `publish()` another event (nested delivery).
- **Order:** `publish` goes in subscription order. Queued events are first-in-first-out per producing thread; between threads the order is whatever the lock decides.
- **Lifetime:** the bus must outlive its subscriptions (asserted). Declare `Subscription` members last in a class, so they're destroyed before the members their handler uses.

## How it works

- **Subscribers** are kept per event type, as lists of `{id, handler, alive}`.
  - A delivery walks the list by index.
  - New subscriptions made during a delivery wait in a pending list, so the list being walked never grows. Resets during a delivery only mark the entry dead.
  - Both are applied when the outermost delivery ends.
- **The queue is double-buffered:** two `BlockArena`s (`MemTag::Events`), each with a list of `{type, pointer, destroy function}`.
  - `enqueue()` takes a mutex, allocates the event in the *back* arena and moves it in.
  - `dispatchQueued()` swaps front and back under the lock, then delivers the front queue **without** the lock, so handlers and other threads can enqueue in the meantime.
  - It then destroys the events and resets the arena. The arena keeps its blocks, so a warmed-up queue makes no heap calls.
- **No lock is held while handlers run** (techstack.md §19).

## Pitfall found while building it

The list of queued events is an ordinary vector, **not** one that allocates from the arena. In MSVC Debug builds even an empty `std::vector` keeps a small bookkeeping object (a "container proxy") in its allocator's memory. `arena.reset()` overwrites freed memory with the `0xDD` debug fill, which destroyed that bookkeeping and crashed in the vector's destructor. The rule that follows: **a container must not outlive the reset of the arena it allocates from**, even when it's empty ([Memory](Memory.md#blockarena)).

## Not yet

- Window events (`WindowResized`, `WindowClosed`, …) and input events: M2, when the platform layer publishes them.
- Priorities, filtering, and delivery to threads other than the main thread.

See also: techstack.md §22, [Memory](Memory.md).
