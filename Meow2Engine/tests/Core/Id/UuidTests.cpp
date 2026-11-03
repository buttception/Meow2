/// @file
/// @brief Tests for Uuid, TypedId and UuidGenerator: layout, text form, ordering, uniqueness across threads.

#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Core/Id/UuidGenerator.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

using Meow2::IdKind;
using Meow2::Uuid;
using Meow2::UuidGenerator;

namespace
{
constexpr std::uint64_t c_node = 0x0012'3456'789A'BCDEULL; // 53 bits, fits the 54-bit node
constexpr std::uint64_t c_time = 1'762'128'000'000ULL;     // 2025-11-03 00:00:00 UTC, in ms
} // namespace

TEST(Uuid, LayoutFields)
{
    UuidGenerator generator{c_node};
    const Uuid id = generator.next(IdKind::Asset, c_time);
    EXPECT_TRUE(id.isValid());
    EXPECT_EQ(id.kind(), IdKind::Asset);
    EXPECT_EQ(id.unixMilliseconds(), c_time);
    EXPECT_EQ((id.high() >> 12) & 0xFU, 8U) << "version 8";
    EXPECT_EQ(id.high() & 0xFFFU, 0U) << "first ID of the millisecond: sequence 0";
    EXPECT_EQ(id.low() >> 62, 0b10U) << "RFC 9562 variant";
    EXPECT_EQ((id.low() >> 54) & 0xFFU, 2U) << "kind Asset";
    EXPECT_EQ(id.low() & UuidGenerator::c_maxNode, c_node) << "the node";
    EXPECT_FALSE(Uuid{}.isValid()) << "the all-zero ID";
    EXPECT_EQ(Uuid{}.kind(), IdKind::Invalid);
}

TEST(Uuid, TextRoundTrip)
{
    UuidGenerator generator{c_node};
    const Uuid id = generator.next(IdKind::Entity, c_time);
    const std::string text = id.toString();
    ASSERT_EQ(text.size(), 36U);
    for (const std::size_t dash : {8U, 13U, 18U, 23U})
    {
        EXPECT_EQ(text[dash], '-') << text;
    }
    EXPECT_EQ(text[14], '8') << "the version digit: " << text;
    EXPECT_TRUE(std::ranges::none_of(text, [](char c) { return c >= 'A' && c <= 'F'; })) << "lowercase: " << text;

    const Meow2::Result<Uuid> parsed = Uuid::parse(text);
    ASSERT_TRUE(parsed.has_value()) << std::format("{}", parsed.error());
    EXPECT_EQ(*parsed, id);

    std::string upper = text;
    std::ranges::transform(upper, upper.begin(),
                           [](char c) { return c >= 'a' && c <= 'f' ? static_cast<char>(c - 'a' + 'A') : c; });
    const Meow2::Result<Uuid> parsedUpper = Uuid::parse(upper);
    ASSERT_TRUE(parsedUpper.has_value());
    EXPECT_EQ(*parsedUpper, id) << "uppercase is accepted too";
}

TEST(Uuid, ParseRejectsBadText)
{
    UuidGenerator generator{c_node};
    const std::string good = generator.next(IdKind::Scene, c_time).toString();
    ASSERT_TRUE(Uuid::parse(good).has_value());

    std::string noDash = good;
    noDash[8] = '0';
    std::string notHex = good;
    notHex[0] = 'g';

    const std::vector<std::string> bad{
        "",
        good.substr(0, 35),
        good + "0",
        noDash,
        notHex,
        "00000000-0000-0000-0000-000000000000", // nil
        "123e4567-e89b-42d3-a456-426614174000", // a random (v4) UUID from elsewhere
        "019a4b1c-7d2e-8001-8023-456789abcdef", // version 8, variant 10, kind 0
    };
    for (const std::string& text : bad)
    {
        const Meow2::Result<Uuid> parsed = Uuid::parse(text);
        ASSERT_FALSE(parsed.has_value()) << "'" << text << "' was accepted";
        EXPECT_EQ(parsed.error().code, Meow2::ErrorCode::InvalidArgument);
    }
}

TEST(Uuid, HashAndFormat)
{
    UuidGenerator generator;
    std::unordered_set<Uuid> set;
    std::unordered_set<Meow2::EntityId> entities;
    for (int i = 0; i < 1000; ++i)
    {
        set.insert(generator.next(IdKind::Asset));
        entities.insert(generator.next<IdKind::Entity>());
    }
    EXPECT_EQ(set.size(), 1000U);
    EXPECT_EQ(entities.size(), 1000U);

    const Uuid id = generator.next(IdKind::Prefab);
    EXPECT_EQ(std::format("{}", id), id.toString());
    EXPECT_EQ(std::format("[{:>40}]", id), std::format("[    {}]", id.toString())) << "width works like a string";
}

TEST(UuidGenerator, IncreasingWithinOneMillisecond)
{
    UuidGenerator generator{c_node};
    Uuid previous;
    for (int i = 0; i < 5000; ++i)
    {
        const Uuid id = generator.next(IdKind::Entity, c_time);
        ASSERT_GT(id, previous) << "ID " << i;
        if (i == 4095)
        {
            EXPECT_EQ(id.unixMilliseconds(), c_time) << "4096 IDs fit in one millisecond";
        }
        if (i == 4096)
        {
            EXPECT_EQ(id.unixMilliseconds(), c_time + 1) << "the 4097th spills into the next millisecond";
            EXPECT_EQ(id.high() & 0xFFFU, 0U);
        }
        previous = id;
    }
}

TEST(UuidGenerator, ClockGoingBackKeepsOrder)
{
    UuidGenerator generator{c_node};
    const Uuid first = generator.next(IdKind::Entity, c_time);
    const Uuid afterClockReset = generator.next(IdKind::Entity, c_time - 60'000); // clock set back a minute
    EXPECT_GT(afterClockReset, first);
    EXPECT_EQ(afterClockReset.unixMilliseconds(), c_time) << "keeps the last time, next sequence number";
}

TEST(UuidGenerator, SortsByTime)
{
    UuidGenerator early{c_node};
    UuidGenerator late{1};
    const Uuid a = early.next(IdKind::Entity, c_time);
    const Uuid b = late.next(IdKind::Entity, c_time + 1);
    EXPECT_LT(a, b) << "a later millisecond sorts later, whatever the node";
}

TEST(UuidGenerator, UniqueAcrossThreads)
{
    constexpr int c_threads = 4;
    constexpr int c_perThread = 20000;
    UuidGenerator generator{c_node};
    std::vector<std::vector<Uuid>> results(c_threads);
    {
        std::vector<std::jthread> threads;
        for (int t = 0; t < c_threads; ++t)
        {
            threads.emplace_back(
                [&generator, &out = results[static_cast<std::size_t>(t)]]
                {
                    out.reserve(c_perThread);
                    for (int i = 0; i < c_perThread; ++i)
                    {
                        out.push_back(generator.next(IdKind::Asset));
                    }
                });
        }
    }
    std::unordered_set<Uuid> all;
    for (const std::vector<Uuid>& ids : results)
    {
        EXPECT_TRUE(std::ranges::is_sorted(ids)) << "each thread sees increasing IDs";
        all.insert(ids.begin(), ids.end());
    }
    EXPECT_EQ(all.size(), static_cast<std::size_t>(c_threads * c_perThread)) << "no ID was handed out twice";
}

TEST(UuidGenerator, RandomNodes)
{
    const UuidGenerator first;
    const UuidGenerator second;
    EXPECT_NE(first.node(), second.node()) << "54 random bits; equal by chance once in 2^54";
    EXPECT_LE(first.node(), UuidGenerator::c_maxNode);
}

TEST(TypedId, KindIsChecked)
{
    UuidGenerator generator{c_node};
    const Meow2::EntityId entity = generator.next<IdKind::Entity>();
    EXPECT_TRUE(entity.isValid());
    EXPECT_EQ(entity.uuid().kind(), IdKind::Entity);
    EXPECT_FALSE(Meow2::EntityId{}.isValid()) << "default = no entity";

    const Uuid asset = generator.next(IdKind::Asset);
    const Meow2::Result<Meow2::EntityId> wrongKind = Meow2::EntityId::fromUuid(asset);
    ASSERT_FALSE(wrongKind.has_value()) << "an asset ID isn't an entity ID";
    EXPECT_EQ(wrongKind.error().code, Meow2::ErrorCode::InvalidArgument);
    EXPECT_FALSE(Meow2::EntityId::fromUuid(Uuid{}).has_value()) << "nor is the all-zero ID";

    const Meow2::Result<Meow2::AssetId> rightKind = Meow2::AssetId::fromUuid(asset);
    ASSERT_TRUE(rightKind.has_value());
    EXPECT_EQ(rightKind->uuid(), asset);
}
