/// Deferred lighting: one full-screen triangle that reads the G-buffer GpuDriven.hlsl's GBUFFER variant wrote and shades
/// each pixel once, with the same lightSurface() the forward path uses. The position isn't stored: it's rebuilt from the
/// pixel's depth (positionFromDepth()). Pixels where nothing was drawn (depth 0, the far end of reversed Z) are left as
/// they are, for the sky.
///
/// g_push.frameBuffer: the frame's FrameConstants; g_push.userData: the heap index of a DeferredInputs record.

#include "Lighting.hlsli"

struct FullscreenOutput
{
    float4 position : SV_Position;
};

/// One triangle that covers the target.
FullscreenOutput vsFullscreen(uint vertexId : SV_VertexID)
{
    FullscreenOutput output;
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(corner * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

float4 psLighting(FullscreenOutput input) : SV_Target
{
    const FrameConstants frame = frameConstants();
    const DeferredInputs inputs = loadElement<DeferredInputs>(g_push.userData, 0);
    const int3 pixel = int3(input.position.xy, 0);

    const Texture2D<float> depthTexture = ResourceDescriptorHeap[inputs.depth];
    const float depth = depthTexture.Load(pixel);
    if (depth <= 0.0)
    {
        discard; // nothing drawn here: the sky pass fills it
    }

    const Texture2D<float4> albedo = ResourceDescriptorHeap[inputs.albedo];
    const Texture2D<float4> normal = ResourceDescriptorHeap[inputs.normal];
    const Texture2D<float4> material = ResourceDescriptorHeap[inputs.material];
    const Texture2D<float4> emissive = ResourceDescriptorHeap[inputs.emissive];
    const float4 properties = material.Load(pixel);

    Surface surface;
    surface.baseColor = albedo.Load(pixel).rgb; // an sRGB texture: read back as linear
    surface.metallic = properties.r;
    surface.roughness = properties.g;
    surface.occlusion = properties.b;
    surface.emissive = emissive.Load(pixel).rgb;
    surface.normal = normalize(normal.Load(pixel).xyz);
    surface.position = positionFromDepth(uint2(pixel.xy), float2(inputs.width, inputs.height), depth,
                                         MEOW2_ROWS(frame, inverseViewProjection));
    return float4(shadeSurface(frame, surface), 1.0);
}
