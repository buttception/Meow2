// Tests of storage textures (RhiDrawTests.cpp): fills one mip level, through that level's own storage slot
// (g_push.userData), with the colour packed into g_push.instance (RGBA8, red in the low byte).
#include "Common.hlsli"

[numthreads(8, 8, 1)]
void csFill(uint3 id : SV_DispatchThreadID)
{
    RWTexture2D<float4> target = ResourceDescriptorHeap[g_push.userData];
    uint width = 0;
    uint height = 0;
    target.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
    {
        return;
    }
    const uint packed = g_push.instance;
    target[id.xy] = float4(packed & 255, (packed >> 8) & 255, (packed >> 16) & 255, packed >> 24) / 255.0;
}
