#pragma once

/// @file
/// @brief GoogleTest printers for the math types, so a failed check prints `(1, 2, 3)` instead of raw bytes.
///
/// GoogleTest finds `PrintTo` by argument-dependent lookup, so they live in the types' namespace. Test-only: the
/// engine itself uses the `std::formatter`s in Meow2/Math/Types.h, which these call.

#include <Meow2/Math/Types.h>

#include <format>
#include <ostream>

namespace Meow2::Math
{
/// @brief Prints a Vec2 for GoogleTest.
inline void PrintTo(const Vec2& value, std::ostream* out)
{
    *out << std::format("{}", value);
}

/// @brief Prints a Vec3 for GoogleTest.
inline void PrintTo(const Vec3& value, std::ostream* out)
{
    *out << std::format("{}", value);
}

/// @brief Prints a Vec4 for GoogleTest.
inline void PrintTo(const Vec4& value, std::ostream* out)
{
    *out << std::format("{}", value);
}

/// @brief Prints a Quat for GoogleTest.
inline void PrintTo(const Quat& value, std::ostream* out)
{
    *out << std::format("{}", value);
}

/// @brief Prints a Mat4 for GoogleTest.
inline void PrintTo(const Mat4& value, std::ostream* out)
{
    *out << std::format("{}", value);
}
} // namespace Meow2::Math
