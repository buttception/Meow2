#pragma once

/// @file
/// @brief The C++ side of the bindless contract: the structs shared with HLSL, from `shaders/Meow2Shared.hlsli`.
///
/// That file is compiled by both languages, so a struct can't drift apart between them. This header supplies HLSL's
/// type names and checks the sizes, and `shaders/Common.hlsli` checks the same sizes on the HLSL side.
/// @see techstack.md §11, docs/wiki/Shaders.md

#include <Meow2/Math/Types.h>

#include <cstddef>
#include <cstdint>

namespace Meow2::Shader
{
/// @brief HLSL's `uint`, for the shared structs.
using uint = std::uint32_t;
/// @brief HLSL's `float4`, for the shared structs.
using float4 = Math::Vec4;
/// @brief HLSL's `float4x4`, row-major on both sides (`-Zpr`), for the shared structs.
using float4x4 = Math::Mat4;

/// @cond
#define MEOW2_MATRIX(name) float4x4 name // a Mat4 is four rows already
#include "Meow2Shared.hlsli"
#undef MEOW2_MATRIX
/// @endcond

// C++'s half of the size checks; shaders/Common.hlsli has the HLSL half.
static_assert(sizeof(PushConstants) == 16, "16 bytes: Vulkan guarantees 128, and D3D12 root constants are costly");
static_assert(sizeof(FrameConstants) == 336);
static_assert(sizeof(InstanceData) == 80);
static_assert(sizeof(IndirectDraw) == 32);
static_assert(offsetof(IndirectDraw, instance) == 0, "D3D12's command signature writes it first");
static_assert(offsetof(IndirectDraw, vertexCount) == 4, "Where the draw arguments start");
static_assert(sizeof(PackedVertex) == 24);
static_assert(sizeof(MaterialData) == 112);
static_assert(sizeof(UiDraw) == 32);
static_assert(sizeof(MeshDraw) == 48);
static_assert(sizeof(CullConstants) == 128);
static_assert(sizeof(SkyConstants) == 160);
static_assert(offsetof(FrameConstants, cameraPosition) == 192);
static_assert(offsetof(InstanceData, material) == 64);
} // namespace Meow2::Shader
