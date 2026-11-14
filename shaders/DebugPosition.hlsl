/// Meow2Renderer's Position debug view (never in games): a compute pass that writes, for every pixel, the position its
/// depth stands for, with exactly the positionFromDepth() deferred lighting uses. Lighting never reads this texture: it
/// rebuilds positions itself. The texture exists only so the reconstruction can be seen and checked.
///
/// g_push.userData is the heap index of a buffer holding one PositionConstants record (layout below; written by
/// DepthPositionView.cpp).

#include "Lighting.hlsli"

/// The pass's inputs. 80 bytes.
struct PositionConstants
{
    float4 inverseRow0; ///< Clip space → the instances' space: the inverse of the view-projection, four rows.
    float4 inverseRow1;
    float4 inverseRow2;
    float4 inverseRow3;
    uint depthTexture;  ///< Heap index of the scene's depth.
    uint output;        ///< Writable heap index of the position texture (RGBA16F).
    uint width;         ///< The textures' size in pixels.
    uint height;
};

[numthreads(8, 8, 1)]
void csPosition(uint3 id : SV_DispatchThreadID)
{
    const PositionConstants constants = loadElement<PositionConstants>(g_push.userData, 0);
    if (id.x >= constants.width || id.y >= constants.height)
    {
        return;
    }
    RWTexture2D<float4> output = ResourceDescriptorHeap[constants.output];
    const Texture2D<float> depthTexture = ResourceDescriptorHeap[constants.depthTexture];
    const float depth = depthTexture.Load(int3(id.xy, 0));
    if (depth <= 0.0)
    {
        output[id.xy] = float4(0.0, 0.0, 0.0, 0.0); // nothing drawn: alpha 0
        return;
    }
    const float4x4 inverse =
        float4x4(constants.inverseRow0, constants.inverseRow1, constants.inverseRow2, constants.inverseRow3);
    const float3 position =
        positionFromDepth(id.xy, float2(constants.width, constants.height), depth, inverse);
    output[id.xy] = float4(position, 1.0);
}
