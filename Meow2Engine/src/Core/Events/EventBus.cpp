/// @file
/// @brief EventBus implementation: subscriber lists per type, a double-buffered queue in two BlockArenas.

#include "Meow2/Core/Events/EventBus.h"

#include "Meow2/Core/Assert.h"
#include "Meow2/Core/Memory/BlockArena.h"
#include "Meow2/Core/Memory/MemTag.h"

#include <algorithm>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Meow2::Events
{
namespace
{
using Memory::MemTag;
using Memory::MemTagScope;

/// @brief One subscribed handler.
struct Subscriber
{
    std::uint64_t id = 0;
    std::function<void(const void*)> handler;
    bool alive = true; ///< false once unsubscribed during a dispatch; removed when the dispatch ends
};

/// @brief One queued event: its type, where it lives in the arena, and how to destroy it.
struct QueuedEvent
{
    EventTypeId type = nullptr;
    void* data = nullptr;
    void (*destroy)(void*) noexcept = nullptr;
};

/// @brief One side of the double buffer: the events' data in an arena, and the list of what's stored where.
///
/// Only the event objects live in the arena. The list is an ordinary vector (allocated under MemTag::Events): a
/// container that allocates from the arena would have its own bookkeeping wiped by reset() (in MSVC Debug builds even
/// an empty vector keeps a small "container proxy" in its allocator's memory). The vector keeps its capacity across
/// frames, so after warm-up neither part allocates.
struct Queue
{
    explicit Queue(std::pmr::memory_resource& upstream)
        : arena(upstream, MemTag::Events)
    {
    }

    /// @brief Destroys the stored events and rewinds the arena (keeping its blocks).
    void clear()
    {
        for (const QueuedEvent& event : events)
        {
            event.destroy(event.data);
        }
        events.clear();
        arena.reset();
    }

    Memory::BlockArena arena;
    std::vector<QueuedEvent> events;
};
} // namespace

/// @brief The bus's state. Subscribers are touched only on the owner thread; the queues are guarded by the mutex.
struct EventBus::Impl
{
    explicit Impl(std::pmr::memory_resource& upstream)
        : queues{Queue{upstream}, Queue{upstream}}
    {
    }

    void assertOwnerThread() const
    {
        MEOW2_ASSERT(std::this_thread::get_id() == owner,
                     "EventBus: subscribe/publish/dispatch on its own thread only");
    }

    /// @brief After the outermost dispatch: adds subscriptions made during it and drops the ones reset during it.
    void settle()
    {
        for (auto& [type, subscribers] : subscribersByType)
        {
            std::erase_if(subscribers, [](const Subscriber& subscriber) { return !subscriber.alive; });
        }
        for (auto& [type, subscriber] : pending)
        {
            subscribersByType[type].push_back(std::move(subscriber));
        }
        pending.clear();
    }

    const std::thread::id owner = std::this_thread::get_id();
    std::unordered_map<EventTypeId, std::vector<Subscriber>> subscribersByType;
    std::vector<std::pair<EventTypeId, Subscriber>> pending; ///< subscribed during a dispatch
    std::uint64_t nextId = 1;
    std::size_t liveCount = 0;
    int dispatchDepth = 0; ///< > 0 while handlers run (publish can nest)
    bool dispatchingQueue = false;

    std::mutex queueMutex; ///< guards `queues` and `backQueue`; never held while handlers run
    Queue queues[2];
    int backQueue = 0; ///< the queue enqueue() writes to
};

Subscription::Subscription(EventBus* bus, std::uint64_t id, EventTypeId type)
    : m_bus(bus)
    , m_id(id)
    , m_type(type)
{
}

Subscription::~Subscription()
{
    reset();
}

Subscription::Subscription(Subscription&& other) noexcept
    : m_bus(std::exchange(other.m_bus, nullptr))
    , m_id(std::exchange(other.m_id, 0))
    , m_type(std::exchange(other.m_type, nullptr))
{
}

Subscription& Subscription::operator=(Subscription&& other) noexcept
{
    if (this != &other)
    {
        reset();
        m_bus = std::exchange(other.m_bus, nullptr);
        m_id = std::exchange(other.m_id, 0);
        m_type = std::exchange(other.m_type, nullptr);
    }
    return *this;
}

void Subscription::reset()
{
    if (m_bus != nullptr)
    {
        m_bus->unsubscribe(m_id, m_type);
        m_bus = nullptr;
    }
}

bool Subscription::active() const
{
    return m_bus != nullptr;
}

EventBus::EventBus(std::pmr::memory_resource& upstream)
    : m_impl(std::make_unique<Impl>(upstream))
{
}

EventBus::~EventBus()
{
    MEOW2_ASSERT(m_impl->liveCount == 0, "destroy every Subscription before its EventBus");
    for (Queue& queue : m_impl->queues)
    {
        queue.clear(); // undelivered events are still destroyed (their strings, vectors, … freed)
    }
}

Subscription EventBus::subscribeErased(EventTypeId type, ErasedHandler handler)
{
    m_impl->assertOwnerThread();
    const MemTagScope tag{MemTag::Events};
    Subscriber subscriber{.id = m_impl->nextId++, .handler = std::move(handler)};
    const std::uint64_t id = subscriber.id;
    if (m_impl->dispatchDepth > 0)
    {
        m_impl->pending.emplace_back(type, std::move(subscriber)); // joins after the current dispatch
    }
    else
    {
        m_impl->subscribersByType[type].push_back(std::move(subscriber));
    }
    ++m_impl->liveCount;
    return Subscription{this, id, type};
}

void EventBus::unsubscribe(std::uint64_t id, EventTypeId type)
{
    m_impl->assertOwnerThread();
    --m_impl->liveCount;
    if (std::erase_if(m_impl->pending, [id](const auto& entry) { return entry.second.id == id; }) > 0)
    {
        return;
    }
    std::vector<Subscriber>& subscribers = m_impl->subscribersByType[type];
    const auto found = std::ranges::find(subscribers, id, &Subscriber::id);
    MEOW2_ASSERT(found != subscribers.end(), "unsubscribing a subscription this bus doesn't know");
    if (m_impl->dispatchDepth > 0)
    {
        found->alive = false; // the list is being iterated; removed in settle()
    }
    else if (found != subscribers.end())
    {
        subscribers.erase(found);
    }
}

void EventBus::publishErased(EventTypeId type, const void* event)
{
    m_impl->assertOwnerThread();
    const auto found = m_impl->subscribersByType.find(type);
    if (found == m_impl->subscribersByType.end())
    {
        return;
    }
    // Iterate by index over a list that can't grow during the dispatch (new subscriptions wait in `pending`), so a
    // handler subscribing or unsubscribing never invalidates the loop.
    ++m_impl->dispatchDepth;
    std::vector<Subscriber>& subscribers = found->second;
    for (std::size_t i = 0; i < subscribers.size(); ++i)
    {
        if (subscribers[i].alive)
        {
            subscribers[i].handler(event);
        }
    }
    if (--m_impl->dispatchDepth == 0)
    {
        m_impl->settle();
    }
}

void EventBus::enqueueErased(EventTypeId type, std::size_t size, std::size_t alignment, MoveConstruct moveConstruct,
                             Destroy destroy, void* event)
{
    const MemTagScope tag{MemTag::Events}; // the event list grows on the heap; count it with the queue
    const std::scoped_lock lock{m_impl->queueMutex};
    Queue& queue = m_impl->queues[m_impl->backQueue];
    void* stored = queue.arena.allocate(size, alignment);
    moveConstruct(stored, event);
    queue.events.push_back({type, stored, destroy});
}

void EventBus::dispatchQueued()
{
    m_impl->assertOwnerThread();
    MEOW2_ASSERT(!m_impl->dispatchingQueue && m_impl->dispatchDepth == 0,
                 "dispatchQueued() can't be called from inside a handler");
    int front = 0;
    {
        // Swap the buffers: from here on enqueue() writes to the other queue, so handlers (and other threads) can
        // enqueue while this one is delivered without the lock.
        const std::scoped_lock lock{m_impl->queueMutex};
        front = m_impl->backQueue;
        m_impl->backQueue ^= 1;
    }
    m_impl->dispatchingQueue = true;
    Queue& queue = m_impl->queues[front];
    for (const QueuedEvent& event : queue.events)
    {
        publishErased(event.type, event.data);
    }
    m_impl->dispatchingQueue = false;
    // Nobody else touches the front queue now: enqueue() only writes to the back one.
    queue.clear();
}

std::size_t EventBus::subscriptionCount() const
{
    return m_impl->liveCount;
}
} // namespace Meow2::Events
