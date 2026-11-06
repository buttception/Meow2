#pragma once

/// @file
/// @brief A seedable random number generator that gives the same sequence on every platform and compiler.
///
/// `Random` is a PCG32 generator (M. O'Neill, pcg-random.org) with the engine's own distributions on top. One seed
/// gives one sequence, bit for bit, on Windows and Linux, MSVC, GCC and Clang, so replays, tests and procedural
/// content can be reproduced.
/// @code
/// Meow2::Math::Random rng{levelSeed};
/// const int damage = rng.range(5, 10);                 // 5..10, both included
/// const Meow2::Math::Vec3 dir = rng.unitVector();      // a uniformly random direction
/// if (rng.chance(0.25F)) { spawnRareLoot(); }
/// @endcode
/// **Never global:** each system (or each job thread) owns its own generator, injected or created from a seed. Two
/// systems sharing one would make each one's sequence depend on how often the other ran.
/// @see techstack.md §10, docs/wiki/Math.md

#include <Meow2/Math/Types.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <random>

namespace Meow2::Math
{
/// @brief PCG32 random number generator with platform-independent distributions.
///
/// - 64 bits of state, 32-bit outputs, period 2⁶⁴ per stream.
/// - **Streams:** generators with the same seed and different streams give unrelated sequences. Use the stream for
///   "one per thread" or "one per system" from a single game seed: `Random{seed, threadIndex}`.
/// - **Why not the standard distributions:** `std::uniform_int_distribution` and friends are implemented differently
///   by each standard library, so the same seed gives different numbers on MSVC and libstdc++. The distributions here
///   use only integer maths and correctly rounded float operations (+, ×, ÷, sqrt; no sin/cos), and the build turns off
///   implicit FMA (`-ffp-contract=off`), so every result is identical everywhere.
/// - It's a UniformRandomBitGenerator, so it works with `<random>` and `<algorithm>`, but `std::shuffle` and the
///   `std::` distributions then make the results platform-dependent again.
/// @warning Not for security: the sequence can be predicted from a few outputs. Use the OS for tokens and keys.
/// @par Thread safety
/// Not thread-safe: one generator per thread (a different stream each).
class Random
{
public:
    /// @brief The output type (UniformRandomBitGenerator).
    using result_type = std::uint32_t;

    /// @brief Creates a generator for a seed and a stream.
    /// @param seed   Any value; the same seed and stream always give the same sequence.
    /// @param stream Selects one of 2⁶³ independent sequences (the top bit is ignored).
    explicit constexpr Random(std::uint64_t seed, std::uint64_t stream = 0) noexcept
        : m_increment((stream << 1U) | 1U)
        , m_seed(seed)
    {
        // PCG's reference seeding: step once from zero, add the seed, step again.
        static_cast<void>(nextU32());
        m_state += seed;
        static_cast<void>(nextU32());
    }

    /// @brief Creates a generator with an unpredictable seed from the OS (`std::random_device`).
    ///
    /// For things that should differ every run. Log the seed (seed()) if a run may need to be reproduced.
    /// @param stream The stream, as in the constructor.
    /// @return The generator.
    [[nodiscard]] static Random fromEntropy(std::uint64_t stream = 0)
    {
        std::random_device device;
        const std::uint64_t seed = (static_cast<std::uint64_t>(device()) << 32U) | device();
        return Random{seed, stream};
    }

    /// @brief The seed this generator was created with.
    /// @return The seed passed to the constructor or chosen by fromEntropy().
    [[nodiscard]] constexpr std::uint64_t seed() const noexcept
    {
        return m_seed;
    }

    /// @brief The next 32 random bits (PCG XSH-RR output).
    /// @return A uniformly distributed 32-bit value.
    constexpr std::uint32_t nextU32() noexcept
    {
        const std::uint64_t old = m_state;
        m_state = old * c_multiplier + m_increment;
        const auto xorShifted = static_cast<std::uint32_t>(((old >> 18U) ^ old) >> 27U);
        const auto rotation = static_cast<std::uint32_t>(old >> 59U);
        return (xorShifted >> rotation) | (xorShifted << ((0U - rotation) & 31U));
    }

    /// @brief The next 64 random bits (two outputs, high first).
    /// @return A uniformly distributed 64-bit value.
    constexpr std::uint64_t nextU64() noexcept
    {
        const std::uint64_t high = nextU32();
        return (high << 32U) | nextU32();
    }

    /// @brief A uniform float in [0, 1).
    ///
    /// The top 24 bits of an output times 2⁻²⁴: every one of the 2²⁴ evenly spaced values is equally likely, and the
    /// result is exact (no rounding), so it's the same everywhere. 1.0 is never returned.
    /// @return A value in [0, 1).
    constexpr float nextFloat() noexcept
    {
        return static_cast<float>(nextU32() >> 8U) * c_floatStep;
    }

    /// @brief A uniform integer in [minValue, maxValue], both included, without bias.
    ///
    /// Uses Lemire's multiply-and-reject method: `nextU32() % n` would favour small values whenever n doesn't divide
    /// 2³², and this doesn't.
    /// @param minValue Smallest possible result.
    /// @param maxValue Largest possible result.
    /// @return A value in [minValue, maxValue].
    /// @pre `minValue <= maxValue` (not checked: the result is then meaningless, but valid).
    constexpr std::int32_t range(std::int32_t minValue, std::int32_t maxValue) noexcept
    {
        // The width as unsigned, so the full int32 range (2³² values) wraps to 0 instead of overflowing.
        const std::uint32_t width = static_cast<std::uint32_t>(maxValue) - static_cast<std::uint32_t>(minValue) + 1U;
        if (width == 0U)
        {
            return static_cast<std::int32_t>(nextU32());
        }
        std::uint64_t product = static_cast<std::uint64_t>(nextU32()) * width;
        auto low = static_cast<std::uint32_t>(product);
        if (low < width)
        {
            // Reject the few products that would make some results more likely: 2³² mod width of them.
            const std::uint32_t threshold = (0U - width) % width;
            while (low < threshold)
            {
                product = static_cast<std::uint64_t>(nextU32()) * width;
                low = static_cast<std::uint32_t>(product);
            }
        }
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(minValue) +
                                         static_cast<std::uint32_t>(product >> 32U));
    }

    /// @brief A uniform float in [minValue, maxValue).
    /// @param minValue Smallest possible result.
    /// @param maxValue The upper end (not reached, except by rounding when the range is huge).
    /// @return A value between them.
    constexpr float range(float minValue, float maxValue) noexcept
    {
        return minValue + (maxValue - minValue) * nextFloat();
    }

    /// @brief `true` with the given probability.
    /// @param probability 0 = never, 1 = always.
    /// @return Whether the event happens.
    constexpr bool chance(float probability) noexcept
    {
        return nextFloat() < probability;
    }

    /// @brief A uniformly random point inside the unit sphere (length ≤ 1).
    ///
    /// Rejection sampling: random points in the cube [-1, 1)³ until one lies in the sphere (52 % hit rate).
    /// @return The point.
    Vec3 pointInSphere() noexcept
    {
        for (;;)
        {
            const Vec3 p{range(-1.0F, 1.0F), range(-1.0F, 1.0F), range(-1.0F, 1.0F)};
            if (lengthSquared(p) <= 1.0F)
            {
                return p;
            }
        }
    }

    /// @brief A uniformly random direction (length 1).
    ///
    /// A point in the sphere, normalised. Points too close to the centre are rejected too, where the direction would
    /// lose precision. (The usual sin/cos formula isn't used: their results differ between C libraries.)
    /// @return The unit vector.
    Vec3 unitVector() noexcept
    {
        for (;;)
        {
            const Vec3 p{range(-1.0F, 1.0F), range(-1.0F, 1.0F), range(-1.0F, 1.0F)};
            const float squared = lengthSquared(p);
            if (squared <= 1.0F && squared > 1e-6F)
            {
                const float length = std::sqrt(squared);
                return {p.x / length, p.y / length, p.z / length};
            }
        }
    }

    /// @brief Smallest output (UniformRandomBitGenerator).
    /// @return 0.
    static constexpr result_type min() noexcept
    {
        return 0;
    }

    /// @brief Largest output (UniformRandomBitGenerator).
    /// @return 2³² − 1.
    static constexpr result_type max() noexcept
    {
        return std::numeric_limits<result_type>::max();
    }

    /// @brief Same as nextU32() (UniformRandomBitGenerator).
    /// @return 32 random bits.
    constexpr result_type operator()() noexcept
    {
        return nextU32();
    }

private:
    /// @brief x² + y² + z², written out in a fixed order so every compiler rounds the same way.
    static constexpr float lengthSquared(const Vec3& p) noexcept
    {
        const float xx = p.x * p.x;
        const float yy = p.y * p.y;
        const float zz = p.z * p.z;
        return (xx + yy) + zz;
    }

    /// @brief PCG's 64-bit LCG multiplier (Knuth's MMIX constant).
    static constexpr std::uint64_t c_multiplier = 6364136223846793005ULL;
    /// @brief 2⁻²⁴: turns a 24-bit integer into [0, 1).
    static constexpr float c_floatStep = 1.0F / 16777216.0F;

    std::uint64_t m_state = 0;     ///< LCG state.
    std::uint64_t m_increment = 1; ///< LCG increment, odd; selects the stream.
    std::uint64_t m_seed = 0;      ///< Seed passed in, for seed().
};
} // namespace Meow2::Math
