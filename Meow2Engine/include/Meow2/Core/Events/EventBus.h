#pragma once

/// @file
/// @brief EventBus: typed events, delivered immediately or queued from any thread, with RAII subscriptions.
///
/// @code
/// struct WindowResized { std::uint32_t width; std::uint32_t height; }; // plain data, no base class
///
/// Meow2::Events::EventBus events{upstreamResource};
/// Meow2::Events::Subscription onResize = events.subscribe<WindowResized>(
///     [&](const WindowResized& e) { swapchain.resize(e.width, e.height); });
///
/// events.enqueue(WindowResized{1600, 900}); // any thread; delivered at the next dispatchQueued()
/// events.publish(WindowResized{1600, 900}); // main thread; delivered now
/// events.dispatchQueued();                  // main thread, at fixed points in the frame
/// @endcode
/// @see techstack.md §22, docs/wiki/Events.md

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <memory_resource>
#include <new>
#include <type_traits>
#include <utility>

namespace Meow2::Events
{
/// @brief Identifies an event type at compile time, without RTTI: the address of a variable that exists once per type.
using EventTypeId = const void*;

/// @cond
namespace Detail
{
template <class T>
inline constexpr char c_eventTypeTag = 0;
} // namespace Detail
/// @endcond

/// @brief The id of event type `T`. Different types always get different ids; the same type always the same one.
/// @tparam T The event type (cv-qualifiers and references are ignored).
/// @return The type's id.
template <class T>
[[nodiscard]] constexpr EventTypeId eventTypeId()
{
    return &Detail::c_eventTypeTag<std::remove_cvref_t<T>>;
}

class EventBus;

/// @brief Keeps a handler subscribed while it lives; destroying (or reset()) unsubscribes it.
///
/// Move-only. Declare subscriptions as the **last** members of a class, so they're destroyed first, before the
/// members their handlers use (techstack.md §22).
/// @par Thread safety
/// Not thread-safe; create, move and destroy it on the bus's thread.
/// @par Lifetime
/// The EventBus must outlive every Subscription made from it (asserted when the bus is destroyed).
class Subscription
{
public:
    /// @brief An empty subscription, subscribed to nothing.
    Subscription() = default;

    /// @brief Unsubscribes, if still subscribed.
    ~Subscription();

    /// @brief Takes over another subscription; `other` becomes empty.
    /// @param other Subscription to move from.
    Subscription(Subscription&& other) noexcept;

    /// @brief Unsubscribes this one, then takes over `other`, which becomes empty.
    /// @param other Subscription to move from.
    /// @return `*this`.
    Subscription& operator=(Subscription&& other) noexcept;

    Subscription(const Subscription&) = delete;            ///< Not copyable: one handler, one owner.
    Subscription& operator=(const Subscription&) = delete; ///< Not copyable: one handler, one owner.

    /// @brief Unsubscribes now. Inside a dispatch the handler isn't called again, not even for the current event.
    void reset();

    /// @brief Whether this still holds a subscription.
    /// @return `false` when empty, reset or moved from.
    [[nodiscard]] bool active() const;

private:
    friend class EventBus;
    Subscription(EventBus* bus, std::uint64_t id, EventTypeId type);

    EventBus* m_bus = nullptr;
    std::uint64_t m_id = 0;
    EventTypeId m_type = nullptr;
};

/// @brief Delivers events by type: publish() immediately, or enqueue() from any thread for the next dispatchQueued().
///
/// - **Events are plain data structs:** no base class, no listener interface. The type is the channel.
/// - **Immediate:** publish() calls the handlers of that type right away, in subscription order.
/// - **Queued:** enqueue() copies the event into the bus's own BlockArena (counted under MemTag::Events), from any
///   thread. dispatchQueued() delivers the queue in order (first-in-first-out per producing thread), then destroys
///   the events and resets the arena. The composition root calls it at fixed points in the frame (techstack.md §23).
/// - **Safe during dispatch:** a subscription made in a handler gets events from the next one on; a subscription
///   reset in a handler isn't called again; an event enqueued in a handler arrives at the *next* dispatchQueued().
///   Handlers may publish() other events (nested dispatch).
/// - No lock is held while handlers run, so a handler may enqueue() freely.
///
/// Event types must be move-constructible and nothrow-destructible. Their move constructor runs under the queue lock,
/// so it must not call back into the bus (plain data structs never do).
/// @par Thread safety
/// enqueue() is thread-safe. Everything else (subscribe, publish, dispatchQueued, destroying Subscriptions) must
/// happen on the thread that created the bus, usually the main thread (asserted).
/// @par Lifetime
/// The upstream resource must outlive the bus; the bus must outlive its Subscriptions.
class EventBus
{
public:
    /// @brief Creates an empty bus. Records the calling thread as its owner.
    /// @param upstream Where the queue arenas get their blocks, e.g. Memory::globalTracker(). Must outlive the bus.
    explicit EventBus(std::pmr::memory_resource& upstream);

    /// @brief Destroys any still-queued events without delivering them.
    /// @pre No Subscription is active any more (asserted).
    ~EventBus();

    EventBus(const EventBus&) = delete;            ///< Not copyable: subscriptions point at it.
    EventBus& operator=(const EventBus&) = delete; ///< Not copyable: subscriptions point at it.
    EventBus(EventBus&&) = delete;                 ///< Not movable: subscriptions point at it.
    EventBus& operator=(EventBus&&) = delete;      ///< Not movable: subscriptions point at it.

    /// @brief Subscribes a handler to events of type `T`.
    /// @tparam T Event type.
    /// @tparam Handler A copyable callable taking `const T&`.
    /// @param handler Called for every `T` published or dispatched while the subscription lives.
    /// @return The subscription; the handler is unsubscribed when it's destroyed.
    template <class T, class Handler>
    [[nodiscard]] Subscription subscribe(Handler&& handler)
    {
        static_assert(std::is_invocable_v<Handler&, const T&>, "the handler must take const T&");
        return subscribeErased(eventTypeId<T>(), [callback = std::forward<Handler>(handler)](const void* event)
                               { callback(*static_cast<const T*>(event)); });
    }

    /// @brief Delivers an event to its subscribers now, before returning.
    /// @tparam T Event type.
    /// @param event The event; handlers get it by const reference.
    template <class T>
    void publish(const T& event)
    {
        publishErased(eventTypeId<T>(), &event);
    }

    /// @brief Queues an event for the next dispatchQueued(). Thread-safe.
    /// @tparam T Event type: move-constructible, nothrow-destructible.
    /// @param event The event, moved into the queue.
    template <class T>
    void enqueue(T event)
    {
        static_assert(std::is_move_constructible_v<T>, "queued events are moved into the queue");
        static_assert(std::is_nothrow_destructible_v<T>, "queued events are destroyed after delivery");
        enqueueErased(
            eventTypeId<T>(), sizeof(T), alignof(T),
            [](void* destination, void* source) { ::new (destination) T(std::move(*static_cast<T*>(source))); },
            [](void* stored) noexcept { static_cast<T*>(stored)->~T(); }, &event);
    }

    /// @brief Delivers every event enqueued before this call, in order, then frees the queue.
    ///
    /// Events enqueued while it runs (by handlers or other threads) wait for the next call.
    /// @pre Not called from inside a handler (asserted).
    void dispatchQueued();

    /// @brief How many subscriptions are active, for all types.
    /// @return The number of live subscriptions.
    [[nodiscard]] std::size_t subscriptionCount() const;

private:
    friend class Subscription;

    using ErasedHandler = std::function<void(const void*)>;
    using MoveConstruct = void (*)(void* destination, void* source);
    using Destroy = void (*)(void* stored) noexcept;

    Subscription subscribeErased(EventTypeId type, ErasedHandler handler);
    void unsubscribe(std::uint64_t id, EventTypeId type);
    void publishErased(EventTypeId type, const void* event);
    void enqueueErased(EventTypeId type, std::size_t size, std::size_t alignment, MoveConstruct moveConstruct,
                       Destroy destroy, void* event);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace Meow2::Events
