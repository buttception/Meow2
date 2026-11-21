/// The editor's selection outline: draws an outline over the scene's HDR colour around what the selection mask covers
/// (GpuDriven.hlsl's psSelectionMask drew 1 there). A pixel outside the mask with a covered pixel within c_width pixels
/// gets the outline colour; everything else is left alone (discarded). The editor draws it into the editor viewport only.
/// g_push.instance is the heap index of the mask (R8).

#include "Common.hlsli"

struct VertexOutput
{
    float4 position : SV_Position;
};

/// One triangle that covers the screen: (-1, -1), (3, -1), (-1, 3).
VertexOutput vsMain(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(corner * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

static const int c_width = 2;                              ///< The outline's width in pixels.
static const float3 c_colour = float3(1.0, 0.42, 0.05);    ///< Orange in linear light; the tone curve keeps it bright.

float4 psMain(VertexOutput input) : SV_Target
{
    Texture2D<float> mask = ResourceDescriptorHeap[g_push.instance];
    const int2 pixel = int2(input.position.xy);
    if (mask.Load(int3(pixel, 0)) > 0.5)
    {
        discard; // inside the selection: the object itself stays visible
    }
    uint width;
    uint height;
    mask.GetDimensions(width, height);
    for (int y = -c_width; y <= c_width; ++y)
    {
        for (int x = -c_width; x <= c_width; ++x)
        {
            const int2 near = clamp(pixel + int2(x, y), int2(0, 0), int2(width, height) - 1);
            if (mask.Load(int3(near, 0)) > 0.5)
            {
                return float4(c_colour, 1.0);
            }
        }
    }
    discard;
    return float4(0.0, 0.0, 0.0, 0.0);
}
