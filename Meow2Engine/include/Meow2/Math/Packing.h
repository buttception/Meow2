#pragma once

/// @file
/// @brief Vertex packing and quantisation: half floats, normalised integers, octahedral normals, tangent frames as one
///        quaternion, and 16-bit positions inside a mesh's bounds.
///
/// A vertex with a float position, normal, tangent and UV takes 48 bytes; packed it takes about 20. The GPU unpacks
/// most of these formats for free while fetching vertices (`R16G16_SNORM`, `R16G16B16A16_UNORM`, `R16_FLOAT`, …), so
/// smaller vertices mean less memory, less bandwidth and faster rendering. Every function here states its error.
///
/// The integer rules match D3D12 and Vulkan exactly, so data packed on the CPU decodes on the GPU to the same values.
/// @code
/// using namespace Meow2::Math;
/// vertex.normal = packOctahedral(normal);                          // 4 bytes instead of 12
/// vertex.frame = packTangentFrame(normal, tangent, handedness);    // normal + tangent + sign in 8 bytes
/// vertex.position = quantizePosition(position, meshBounds);        // 6 bytes instead of 12
/// @endcode
/// @see techstack.md §10, docs/wiki/Math.md

#include <Meow2/Math/Bounds.h>
#include <Meow2/Math/Quaternion.h>
#include <Meow2/Math/Scalar.h>
#include <Meow2/Math/Types.h>

#include <DirectXPackedVector.h>

#include <array>
#include <cmath>
#include <cstdint>

namespace Meow2::Math
{
/// @brief Converts a float to a 16-bit half float (IEEE 754 binary16), rounding to nearest.
///
/// Halves keep 11 significant bits (about 3 decimal digits) and reach ±65504; larger values become infinity. Good for
/// UVs, colours and anything with a small range.
/// @param value The float.
/// @return The half's bits.
[[nodiscard]] inline std::uint16_t toHalf(float value) noexcept
{
    return DirectX::PackedVector::XMConvertFloatToHalf(value);
}

/// @brief Converts a 16-bit half float back to a float (exact: every half is a float).
/// @param bits The half's bits.
/// @return The value.
[[nodiscard]] inline float fromHalf(std::uint16_t bits) noexcept
{
    return DirectX::PackedVector::XMConvertHalfToFloat(bits);
}

/// @brief Packs 0..1 into 8 bits (UNORM8): 0 → 0, 1 → 255, rounded to nearest.
/// @param value The value; clamped to 0..1.
/// @return The byte.
[[nodiscard]] inline std::uint8_t packUnorm8(float value) noexcept
{
    return static_cast<std::uint8_t>(std::lround(saturate(value) * 255.0F));
}

/// @brief Unpacks UNORM8: `byte / 255`, as the GPU does.
/// @param packed The byte.
/// @return The value, 0..1.
[[nodiscard]] inline float unpackUnorm8(std::uint8_t packed) noexcept
{
    return static_cast<float>(packed) / 255.0F;
}

/// @brief Packs 0..1 into 16 bits (UNORM16): 0 → 0, 1 → 65535, rounded to nearest.
/// @param value The value; clamped to 0..1.
/// @return The packed value; error at most 1 / 131070.
[[nodiscard]] inline std::uint16_t packUnorm16(float value) noexcept
{
    return static_cast<std::uint16_t>(std::lround(saturate(value) * 65535.0F));
}

/// @brief Unpacks UNORM16: `value / 65535`.
/// @param packed The packed value.
/// @return The value, 0..1.
[[nodiscard]] inline float unpackUnorm16(std::uint16_t packed) noexcept
{
    return static_cast<float>(packed) / 65535.0F;
}

/// @brief Packs −1..1 into 8 bits (SNORM8): −1 → −127, 1 → 127, rounded to nearest.
///
/// −128 is never produced: D3D12 and Vulkan decode both −128 and −127 to −1, so 0 stays exactly 0.
/// @param value The value; clamped to −1..1.
/// @return The packed value.
[[nodiscard]] inline std::int8_t packSnorm8(float value) noexcept
{
    return static_cast<std::int8_t>(std::lround(clamp(value, -1.0F, 1.0F) * 127.0F));
}

/// @brief Unpacks SNORM8 the way the GPU does: `max(value / 127, −1)`.
/// @param packed The packed value.
/// @return The value, −1..1.
[[nodiscard]] inline float unpackSnorm8(std::int8_t packed) noexcept
{
    const float value = static_cast<float>(packed) / 127.0F;
    return value < -1.0F ? -1.0F : value;
}

/// @brief Packs −1..1 into 16 bits (SNORM16): −1 → −32767, 1 → 32767.
/// @param value The value; clamped to −1..1.
/// @return The packed value; error at most 1 / 65534.
[[nodiscard]] inline std::int16_t packSnorm16(float value) noexcept
{
    return static_cast<std::int16_t>(std::lround(clamp(value, -1.0F, 1.0F) * 32767.0F));
}

/// @brief Unpacks SNORM16: `max(value / 32767, −1)`.
/// @param packed The packed value.
/// @return The value, −1..1.
[[nodiscard]] inline float unpackSnorm16(std::int16_t packed) noexcept
{
    const float value = static_cast<float>(packed) / 32767.0F;
    return value < -1.0F ? -1.0F : value;
}

/// @brief Packs a unit normal into 32 bits: octahedral mapping into two SNORM16 values (x in the low half).
///
/// The sphere of directions is projected onto an octahedron and unfolded into a square, which spreads the 2³² codes
/// almost evenly over all directions: the worst error is under 0.0001 rad (0.006°, tested). The GPU format is
/// `R16G16_SNORM`; the shader decodes it with a few instructions.
/// @param normal A unit vector.
/// @return The packed normal.
[[nodiscard]] inline std::uint32_t packOctahedral(const Vec3& normal) noexcept
{
    const float l1 = std::abs(normal.x) + std::abs(normal.y) + std::abs(normal.z);
    float x = normal.x / l1;
    float y = normal.y / l1;
    if (normal.z < 0.0F)
    {
        // Fold the lower half of the octahedron over the upper one, into the square's corners.
        const float foldedX = (1.0F - std::abs(y)) * (x >= 0.0F ? 1.0F : -1.0F);
        const float foldedY = (1.0F - std::abs(x)) * (y >= 0.0F ? 1.0F : -1.0F);
        x = foldedX;
        y = foldedY;
    }
    return static_cast<std::uint32_t>(static_cast<std::uint16_t>(packSnorm16(x))) |
           (static_cast<std::uint32_t>(static_cast<std::uint16_t>(packSnorm16(y))) << 16U);
}

/// @brief Unpacks an octahedral normal (packOctahedral()).
/// @param packed The packed normal.
/// @return The unit vector.
[[nodiscard]] inline Vec3 unpackOctahedral(std::uint32_t packed) noexcept
{
    const float x = unpackSnorm16(static_cast<std::int16_t>(static_cast<std::uint16_t>(packed & 0xFFFFU)));
    const float y = unpackSnorm16(static_cast<std::int16_t>(static_cast<std::uint16_t>(packed >> 16U)));
    const float z = 1.0F - std::abs(x) - std::abs(y);
    // Points outside the inner diamond were folded: unfold them (the same formula is its own inverse there).
    const float unfold = z < 0.0F ? -z : 0.0F;
    const Vec3 v{x + (x >= 0.0F ? -unfold : unfold), y + (y >= 0.0F ? -unfold : unfold), z};
    return storeVec3(DirectX::XMVector3Normalize(load(v)));
}

/// @brief A tangent frame packed as a quaternion in four SNORM16 values (8 bytes). Plain data.
struct PackedTangentFrame
{
    std::array<std::int16_t, 4> components{0, 0, 0,
                                           32767}; ///< x, y, z, w of the frame's rotation; w's sign is the handedness.
};

/// @brief An unpacked tangent frame. Plain data.
struct TangentFrame
{
    Vec3 normal;             ///< Unit normal.
    Vec3 tangent;            ///< Unit tangent (along the texture's u), perpendicular to the normal.
    float handedness = 1.0F; ///< +1 or −1: bitangent = handedness × cross(normal, tangent).
};

/// @brief Packs normal, tangent and handedness into one quaternion (a "QTangent", 8 bytes instead of 28).
///
/// The three axes tangent, cross(normal, tangent) and normal form a rotation; it's stored as a quaternion with
/// `w ≥ 0`, and the handedness (mirrored UVs) is stored as the sign of `w`, since `q` and `−q` are the same rotation.
/// A tiny minimum |w| keeps the sign readable when the rotation's w is 0. Error: about 0.0001 rad per axis.
/// @param normal     Unit normal.
/// @param tangent    Unit tangent, perpendicular to the normal (re-orthogonalised here if it's slightly off).
/// @param handedness +1 or −1 (the sign of glTF's `tangent.w` after the importer's conversion).
/// @return The packed frame.
[[nodiscard]] inline PackedTangentFrame packTangentFrame(const Vec3& normal, const Vec3& tangent,
                                                         float handedness) noexcept
{
    using namespace DirectX;
    const XMVECTOR n = XMVector3Normalize(load(normal));
    // Gram-Schmidt: remove any part of the tangent along the normal.
    const XMVECTOR t = XMVector3Normalize(
        XMVectorSubtract(load(tangent), XMVectorScale(n, XMVectorGetX(XMVector3Dot(n, load(tangent))))));
    const XMVECTOR b = XMVector3Cross(n, t);
    const XMMATRIX basis{t, b, n, g_XMIdentityR3}; // rows: where +X, +Y and +Z end up
    Quat q = normalize(storeQuat(XMQuaternionRotationMatrix(basis)));
    if (q.w < 0.0F)
    {
        q = Quat{-q.x, -q.y, -q.z, -q.w};
    }
    // The smallest positive SNORM16 step; scale the vector part so the quaternion stays unit length.
    constexpr float c_minW = 1.0F / 32767.0F;
    if (q.w < c_minW)
    {
        const float scale = std::sqrt(1.0F - c_minW * c_minW);
        q = Quat{q.x * scale, q.y * scale, q.z * scale, c_minW};
    }
    const float sign = handedness < 0.0F ? -1.0F : 1.0F;
    return PackedTangentFrame{
        {packSnorm16(q.x * sign), packSnorm16(q.y * sign), packSnorm16(q.z * sign), packSnorm16(q.w * sign)}};
}

/// @brief Unpacks a tangent frame (packTangentFrame()).
/// @param packed The packed frame.
/// @return Normal, tangent and handedness.
[[nodiscard]] inline TangentFrame unpackTangentFrame(const PackedTangentFrame& packed) noexcept
{
    const Quat q = normalize(Quat{unpackSnorm16(packed.components[0]), unpackSnorm16(packed.components[1]),
                                  unpackSnorm16(packed.components[2]), unpackSnorm16(packed.components[3])});
    return TangentFrame{rotate(q, c_forward), rotate(q, c_right), packed.components[3] < 0 ? -1.0F : 1.0F};
}

/// @brief A position quantised to 16 bits per axis inside a box (6 bytes instead of 12). Plain data.
struct QuantizedPosition
{
    std::array<std::uint16_t, 3> components{}; ///< x, y, z as UNORM16 across the box.
};

/// @brief Quantises a position to 16 bits per axis inside a mesh's bounds.
///
/// The renderer scales the result back with the bounds (one multiply-add in the vertex shader, or folded into the
/// world matrix). The error is at most half a step: `extents / 65535` per axis (positionQuantizationError()), e.g.
/// 0.015 mm for a 2 m object.
/// @param position A position inside `bounds` (positions outside are clamped to it).
/// @param bounds   The mesh's bounding box.
/// @return The quantised position.
[[nodiscard]] inline QuantizedPosition quantizePosition(const Vec3& position, const Aabb& bounds) noexcept
{
    const auto axis = [](float p, float center, float extent)
    {
        return extent > 0.0F ? packUnorm16((p - (center - extent)) / (2.0F * extent)) : std::uint16_t{0};
    };
    return QuantizedPosition{{axis(position.x, bounds.center.x, bounds.extents.x),
                              axis(position.y, bounds.center.y, bounds.extents.y),
                              axis(position.z, bounds.center.z, bounds.extents.z)}};
}

/// @brief Restores a quantised position (quantizePosition()).
/// @param quantized The quantised position.
/// @param bounds    The same bounds it was quantised with.
/// @return The position, within positionQuantizationError() of the original.
[[nodiscard]] inline Vec3 dequantizePosition(const QuantizedPosition& quantized, const Aabb& bounds) noexcept
{
    const auto axis = [](std::uint16_t q, float center, float extent)
    {
        return center - extent + unpackUnorm16(q) * 2.0F * extent;
    };
    return Vec3{axis(quantized.components[0], bounds.center.x, bounds.extents.x),
                axis(quantized.components[1], bounds.center.y, bounds.extents.y),
                axis(quantized.components[2], bounds.center.z, bounds.extents.z)};
}

/// @brief The largest error of quantizePosition() per axis for some bounds: half a 16-bit step.
/// @param bounds The bounds.
/// @return `extents / 65535` per axis.
[[nodiscard]] inline Vec3 positionQuantizationError(const Aabb& bounds) noexcept
{
    return Vec3{bounds.extents.x / 65535.0F, bounds.extents.y / 65535.0F, bounds.extents.z / 65535.0F};
}
} // namespace Meow2::Math
