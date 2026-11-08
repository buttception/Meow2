// The bindless contract (techstack.md §11): every shader includes this file.
//
// There's one binding layout in the whole engine: push constants, and the global descriptor heap. A shader reads the
// indices it needs from g_push and fetches its resources from the heap with the helpers below. Nothing else is bound,
// which is what lets a compute shader drive thousands of draws.
#ifndef MEOW2_COMMON_HLSLI
#define MEOW2_COMMON_HLSLI

#include "Meow2Shared.hlsli"

// Vulkan: push constants. D3D12: root constants in b0. The attribute is ignored when compiling for D3D12.
[[vk::push_constant]] ConstantBuffer<PushConstants> g_push : register(b0);

/// A raw buffer from the heap. Buffers are ByteAddressBuffers, so one heap slot type serves every struct.
ByteAddressBuffer heapBuffer(uint index)
{
    ByteAddressBuffer buffer = ResourceDescriptorHeap[index];
    return buffer;
}

/// A sampler from the sampler heap. Slot 0 is linear filtering with repeat addressing.
SamplerState heapSampler(uint index)
{
    SamplerState sampler = SamplerDescriptorHeap[index];
    return sampler;
}

/// The frame's constants.
FrameConstants frameConstants()
{
    return heapBuffer(g_push.frameBuffer).Load<FrameConstants>(0);
}

/// The current instance.
InstanceData instanceData()
{
    return heapBuffer(g_push.instanceBuffer).Load<InstanceData>(g_push.instance * sizeof(InstanceData));
}

/// The index of the instance an indirect draw (IndirectDraw) is for, from the vertex shader's SV_InstanceID. Vulkan's
/// SV_InstanceID includes the draw's first instance; D3D12's starts at 0 for every draw, so there the command signature
/// writes the index into g_push.instance instead.
uint indirectInstance(uint instanceId)
{
#ifdef __spirv__
    return instanceId;
#else
    return g_push.instance + instanceId;
#endif
}

/// Element `element` of an array of T in a buffer from the heap.
template <typename T>
T loadElement(uint bufferIndex, uint element)
{
    return heapBuffer(bufferIndex).Load<T>(element * sizeof(T));
}

// HLSL's half of the size checks; BindlessLayout.h has the C++ half.
// A wrong size makes an array of negative length, a compile error: DXC has no global _Static_assert.
typedef uint Meow2CheckPushConstants[sizeof(PushConstants) == 16 ? 1 : -1];
typedef uint Meow2CheckFrameConstants[sizeof(FrameConstants) == 336 ? 1 : -1];
typedef uint Meow2CheckInstanceData[sizeof(InstanceData) == 80 ? 1 : -1];
typedef uint Meow2CheckIndirectDraw[sizeof(IndirectDraw) == 32 ? 1 : -1];
typedef uint Meow2CheckPackedVertex[sizeof(PackedVertex) == 24 ? 1 : -1];
typedef uint Meow2CheckMaterialData[sizeof(MaterialData) == 272 ? 1 : -1];
typedef uint Meow2CheckUiDraw[sizeof(UiDraw) == 32 ? 1 : -1];
typedef uint Meow2CheckMeshDraw[sizeof(MeshDraw) == 48 ? 1 : -1];
typedef uint Meow2CheckCullConstants[sizeof(CullConstants) == 128 ? 1 : -1];
typedef uint Meow2CheckSkyConstants[sizeof(SkyConstants) == 160 ? 1 : -1];

#endif
