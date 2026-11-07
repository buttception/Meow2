// Structs shared by HLSL and C++ (techstack.md §11). One definition, compiled by both languages: C++ includes this file
// through <Meow2/Graphics/Shader/BindlessLayout.h>, which supplies the types below and checks the sizes.
//
// Rules, so both sides lay a struct out the same way:
// - only uint, float4 and float4x4 (row-major in both: -Zpr);
// - no float3 or float2: HLSL packs them differently from C++;
// - pad by hand to a multiple of 16 bytes.
#ifndef MEOW2_SHARED_HLSLI
#define MEOW2_SHARED_HLSLI

/// The only per-draw binding in the engine: indices into the global descriptor heap, in 16 bytes of push constants.
struct PushConstants
{
    uint frameBuffer;    ///< Heap index of a ByteAddressBuffer holding one FrameConstants.
    uint instanceBuffer; ///< Heap index of a ByteAddressBuffer holding the InstanceData array.
    uint instance;       ///< Index of the instance in that array.
    uint userData;       ///< One free value for the pass.
};

/// What every shader of a view needs, once per frame.
struct FrameConstants
{
    float4x4 view;           ///< World (camera-relative) → view space.
    float4x4 projection;     ///< View → clip space, reversed Z.
    float4x4 viewProjection; ///< view × projection.
    float4 cameraPosition;   ///< xyz: the camera in world space; w: unused.
    float4 viewport;         ///< xy: size in pixels; zw: 1 / size.
};

/// One drawn object, in a buffer the GPU reads and compute culling can edit.
struct InstanceData
{
    float4x4 world;   ///< Local → camera-relative world.
    uint material;    ///< Index of the material.
    uint mesh;        ///< Index of the mesh.
    uint padding0;    ///< Pads to 16 bytes.
    uint padding1;    ///< Pads to 16 bytes.
};

#endif
