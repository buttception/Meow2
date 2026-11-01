/// @file
/// @brief Tests for EventBus and Subscription.

#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Memory/MallocResource.h>
#include <Meow2/Core/Memory/TrackingAllocator.h>

#include <gtest/gtest.h>

#include <memory_resource>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace
{
using Meow2::Events::EventBus;
using Meow2::Events::Subscription;

struct Resized
{
    int width = 0;
    int height = 0;
};

struct Clicked
{
    int button = 0;
};

struct Message
{
    std::string text; ///< non-trivial: allocates, must be destroyed
};

/// @brief An event that counts how many of its objects are alive, to check queued events are destroyed.
struct Counted
{
    explicit Counted(int& liveObjects)
        : live(&liveObjects)
    {
        ++*live;
    }
    Counted(Counted&& other) noexcept
        : live(other.live)
    {
        ++*live;
    }
    Counted(const Counted&) = delete;
    Counted& operator=(const Counted&) = delete;
    Counted& operator=(Counted&&) = delete;
    ~Counted()
    {
        --*live;
    }

    int* live;
};

/// @brief An event from one of the producer threads.
struct Numbered
{
    int producer = 0;
    int sequence = 0;
};

/// @brief A bus over a tracker, so tests can see what the queue allocates.
class EventBusTest : public ::testing::Test
{
protected:
    Meow2::Memory::MallocResource m_malloc;
    Meow2::Memory::TrackingAllocator m_tracker{m_malloc};
    std::optional<EventBus> m_bus{std::in_place, m_tracker};

    EventBus& bus()
    {
        return *m_bus;
    }
};
} // namespace

TEST_F(EventBusTest, PublishCallsSubscribers)
{
    std::vector<int> calls;
    const Subscription first = bus().subscribe<Resized>([&](const Resized& e) { calls.push_back(e.width); });
    const Subscription second = bus().subscribe<Resized>([&](const Resized& e) { calls.push_back(e.height); });
    bus().publish(Resized{800, 600});
    EXPECT_EQ(calls, (std::vector<int>{800, 600})) << "both handlers, in subscription order";
    EXPECT_EQ(bus().subscriptionCount(), 2u);
}

TEST_F(EventBusTest, TypesAreSeparate)
{
    int resized = 0;
    int clicked = 0;
    const Subscription onResize = bus().subscribe<Resized>([&](const Resized&) { ++resized; });
    const Subscription onClick = bus().subscribe<Clicked>([&](const Clicked&) { ++clicked; });
    bus().publish(Clicked{1});
    EXPECT_EQ(resized, 0);
    EXPECT_EQ(clicked, 1);
    static_assert(Meow2::Events::eventTypeId<Resized>() != Meow2::Events::eventTypeId<Clicked>());
    static_assert(Meow2::Events::eventTypeId<const Resized&>() == Meow2::Events::eventTypeId<Resized>());
}

TEST_F(EventBusTest, SubscriptionUnsubscribes)
{
    int calls = 0;
    {
        const Subscription scoped = bus().subscribe<Clicked>([&](const Clicked&) { ++calls; });
        bus().publish(Clicked{});
    }
    bus().publish(Clicked{});
    EXPECT_EQ(calls, 1) << "destroyed: unsubscribed";

    Subscription manual = bus().subscribe<Clicked>([&](const Clicked&) { ++calls; });
    manual.reset();
    EXPECT_FALSE(manual.active());
    bus().publish(Clicked{});
    EXPECT_EQ(calls, 1) << "reset: unsubscribed";
    EXPECT_EQ(bus().subscriptionCount(), 0u);
}

TEST_F(EventBusTest, SubscriptionMoves)
{
    int calls = 0;
    Subscription original = bus().subscribe<Clicked>([&](const Clicked&) { ++calls; });
    Subscription moved = std::move(original);
    EXPECT_FALSE(original.active()); // NOLINT(bugprone-use-after-move): checking the moved-from state is the test
    EXPECT_TRUE(moved.active());
    bus().publish(Clicked{});
    EXPECT_EQ(calls, 1);

    Subscription target = bus().subscribe<Clicked>([&](const Clicked&) { calls += 100; });
    target = std::move(moved); // the +100 handler is unsubscribed, the +1 one moves in
    bus().publish(Clicked{});
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(bus().subscriptionCount(), 1u);
}

TEST_F(EventBusTest, UnsubscribeDuringDispatch)
{
    std::vector<std::string> calls;
    Subscription second;
    const Subscription first = bus().subscribe<Clicked>(
        [&](const Clicked&)
        {
            calls.emplace_back("first");
            second.reset(); // the next handler in the same dispatch must not run
        });
    second = bus().subscribe<Clicked>([&](const Clicked&) { calls.emplace_back("second"); });
    bus().publish(Clicked{});
    bus().publish(Clicked{});
    EXPECT_EQ(calls, (std::vector<std::string>{"first", "first"}));
    EXPECT_EQ(bus().subscriptionCount(), 1u);
}

TEST_F(EventBusTest, SubscribeDuringDispatch)
{
    int lateCalls = 0;
    Subscription late;
    const Subscription first = bus().subscribe<Clicked>(
        [&](const Clicked&)
        {
            if (!late.active())
            {
                late = bus().subscribe<Clicked>([&](const Clicked&) { ++lateCalls; });
            }
        });
    bus().publish(Clicked{});
    EXPECT_EQ(lateCalls, 0) << "a subscription made during a dispatch doesn't get that event";
    bus().publish(Clicked{});
    EXPECT_EQ(lateCalls, 1);
}

TEST_F(EventBusTest, NestedPublish)
{
    std::vector<std::string> calls;
    const Subscription onClick = bus().subscribe<Clicked>(
        [&](const Clicked&)
        {
            calls.emplace_back("clicked");
            bus().publish(Resized{});
        });
    const Subscription onResize = bus().subscribe<Resized>([&](const Resized&) { calls.emplace_back("resized"); });
    bus().publish(Clicked{});
    EXPECT_EQ(calls, (std::vector<std::string>{"clicked", "resized"}));
}

TEST_F(EventBusTest, EnqueueIsDeliveredOnDispatch)
{
    std::vector<std::string> received;
    const Subscription onMessage = bus().subscribe<Message>([&](const Message& m) { received.push_back(m.text); });
    bus().enqueue(Message{"one"});
    bus().enqueue(Message{std::string(200, 'x')}); // too long for the small-string buffer: owns heap memory
    bus().enqueue(Message{"three"});
    EXPECT_TRUE(received.empty()) << "nothing until dispatchQueued()";

    bus().dispatchQueued();
    ASSERT_EQ(received.size(), 3u);
    EXPECT_EQ(received[0], "one");
    EXPECT_EQ(received[1].size(), 200u);
    EXPECT_EQ(received[2], "three");

    bus().dispatchQueued();
    EXPECT_EQ(received.size(), 3u) << "each event is delivered once";
}

TEST_F(EventBusTest, EnqueueFromThreads)
{
    constexpr int c_producers = 4;
    constexpr int c_eventsPerProducer = 1000;
    std::vector<std::vector<int>> received(c_producers);
    const Subscription onNumbered = bus().subscribe<Numbered>(
        [&](const Numbered& e) { received[static_cast<std::size_t>(e.producer)].push_back(e.sequence); });

    std::vector<std::thread> producers;
    for (int producer = 0; producer < c_producers; ++producer)
    {
        producers.emplace_back(
            [this, producer]
            {
                for (int sequence = 0; sequence < c_eventsPerProducer; ++sequence)
                {
                    bus().enqueue(Numbered{producer, sequence});
                }
            });
    }
    // Dispatch while the producers are still running, too: events must never get lost between the two buffers.
    for (int i = 0; i < 20; ++i)
    {
        bus().dispatchQueued();
    }
    for (std::thread& producer : producers)
    {
        producer.join();
    }
    bus().dispatchQueued();

    for (const std::vector<int>& sequences : received)
    {
        ASSERT_EQ(sequences.size(), static_cast<std::size_t>(c_eventsPerProducer));
        for (int i = 0; i < c_eventsPerProducer; ++i)
        {
            ASSERT_EQ(sequences[static_cast<std::size_t>(i)], i) << "first-in-first-out per producing thread";
        }
    }
}

TEST_F(EventBusTest, EnqueueDuringDispatchIsNextDispatch)
{
    int resized = 0;
    const Subscription onClick = bus().subscribe<Clicked>([&](const Clicked&) { bus().enqueue(Resized{}); });
    const Subscription onResize = bus().subscribe<Resized>([&](const Resized&) { ++resized; });
    bus().enqueue(Clicked{});
    bus().dispatchQueued();
    EXPECT_EQ(resized, 0) << "enqueued by a handler: not in this dispatch, so no endless loops";
    bus().dispatchQueued();
    EXPECT_EQ(resized, 1);
}

TEST_F(EventBusTest, QueuedEventsAreDestroyed)
{
    int liveObjects = 0;
    bus().enqueue(Counted{liveObjects});
    EXPECT_EQ(liveObjects, 1) << "the temporary is gone, the queued copy lives";
    bus().dispatchQueued(); // no subscriber: still destroyed after the dispatch
    EXPECT_EQ(liveObjects, 0);

    bus().enqueue(Counted{liveObjects});
    bus().enqueue(Message{std::string(500, 'y')});
    m_bus.reset(); // never dispatched: destroyed with the bus
    EXPECT_EQ(liveObjects, 0);
    EXPECT_EQ(m_tracker.totalStats().liveAllocations, 0u) << "nothing leaked, the long string included";
}

TEST_F(EventBusTest, QueueMemoryIsTagged)
{
    using Meow2::Memory::MemTag;
    bus().enqueue(Resized{1, 2});
    EXPECT_GT(m_tracker.stats(MemTag::Events).currentBytes, 0u) << "the queue arena's blocks";
    EXPECT_EQ(m_tracker.stats(MemTag::General).currentBytes, 0u);
    bus().dispatchQueued();
    EXPECT_GT(m_tracker.stats(MemTag::Events).currentBytes, 0u) << "reset keeps the blocks for the next frame";
}
