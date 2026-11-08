/// Dear ImGui's draw lists, drawn through the bindless heap (Meow2Renderer's UI). Each ImGui draw command is one UiDraw
/// record; g_push.frameBuffer names the record array and g_push.instance the record. The vertex shader pulls ImGui's
/// 20-byte vertices (position, UV, packed colour) and 32-bit indices from buffers, so there's no vertex layout.
/// g_push.userData is the command's first index: D3D12's SV_VertexID leaves out a draw's first vertex, Vulkan's doesn't,
/// so every draw starts at 0 and adds it here.

#include "Common.hlsli"

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation uint texture : TEXTURE0;
};

/// ImGui works in sRGB colours; the target's sRGB view encodes what we write, so decode first.
float3 srgbToLinear(float3 color)
{
    return select(color <= 0.04045, color / 12.92, pow((color + 0.055) / 1.055, 2.4));
}

VertexOutput vsMain(uint vertexId : SV_VertexID)
{
    const UiDraw draw = loadElement<UiDraw>(g_push.frameBuffer, g_push.instance);
    const uint index = loadElement<uint>(draw.indexBuffer, g_push.userData + vertexId) + draw.vertexOffset;
    const ByteAddressBuffer vertices = heapBuffer(draw.vertexBuffer);
    const uint offset = index * 20;
    const float2 position = asfloat(vertices.Load2(offset));
    const float2 uv = asfloat(vertices.Load2(offset + 8));
    const uint packed = vertices.Load(offset + 16);

    VertexOutput output;
    output.position = float4(position * draw.scaleTranslate.xy + draw.scaleTranslate.zw, 0.0, 1.0);
    output.uv = uv;
    const float4 color = float4(packed & 255, (packed >> 8) & 255, (packed >> 16) & 255, packed >> 24) / 255.0;
    output.color = float4(srgbToLinear(color.rgb), color.a);
    output.texture = draw.texture;
    return output;
}

float4 psMain(VertexOutput input) : SV_Target
{
    float4 color = input.color;
    if (input.texture != 0)
    {
        // An sRGB texture: sampling decodes it, like the vertex colour above.
        Texture2D<float4> texture = ResourceDescriptorHeap[NonUniformResourceIndex(input.texture)];
        color *= texture.Sample(heapSampler(0), input.uv);
    }
    return color;
}
