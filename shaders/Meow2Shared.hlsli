// Structs shared by HLSL and C++ (techstack.md §11). One definition, compiled by both languages: C++ includes this file
// through <Meow2/Graphics/Shader/BindlessLayout.h>, which supplies the types below and checks the sizes.
//
// Rules, so both sides lay a struct out the same way:
// - only uint, float4, and matrices through MEOW2_MATRIX: DXC reads a float4x4 from a raw buffer by column whatever -Zpr
//   and row_major say, so matrices are stored as four rows and rebuilt with MEOW2_ROWS, the same on every backend;
// - no float3 or float2: HLSL packs them differently from C++;
// - pad by hand to a multiple of 16 bytes.
#ifndef MEOW2_SHARED_HLSLI
#define MEOW2_SHARED_HLSLI

#ifndef MEOW2_MATRIX
/// A matrix, stored as four rows. C++ sees one Mat4 (BindlessLayout.h defines this macro first).
#define MEOW2_MATRIX(name) float4 name##Row0; float4 name##Row1; float4 name##Row2; float4 name##Row3
/// Rebuilds a matrix stored with MEOW2_MATRIX.
#define MEOW2_ROWS(owner, name) float4x4(owner.name##Row0, owner.name##Row1, owner.name##Row2, owner.name##Row3)
#endif

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
    MEOW2_MATRIX(view);           ///< World (camera-relative) → view space.
    MEOW2_MATRIX(projection);     ///< View → clip space, reversed Z.
    MEOW2_MATRIX(viewProjection); ///< view × projection.
    float4 cameraPosition;   ///< xyz: the camera in world space; w: unused.
    float4 viewport;         ///< xy: size in pixels; zw: 1 / size.
    uint materialBuffer;     ///< Heap index of the MaterialData array; set by GpuScene.
    uint materialCount;      ///< Materials in it. An instance with a higher index gets a colour from its index.
    uint padding0;           ///< Pads to 16 bytes.
    uint padding1;           ///< Pads to 16 bytes.
};

/// One drawn object, in a buffer the GPU reads and compute culling can edit.
struct InstanceData
{
    MEOW2_MATRIX(world); ///< Local → camera-relative world.
    uint material;    ///< Index of the material.
    uint mesh;        ///< Index of the mesh.
    uint padding0;    ///< Pads to 16 bytes.
    uint padding1;    ///< Pads to 16 bytes.
};

/// One indirect draw, written by culling. The four fields from vertexCount on are the layout VkDrawIndirectCommand and
/// D3D12_DRAW_ARGUMENTS require. `instance` comes first because D3D12's SV_InstanceID leaves out the draw's first
/// instance: the command signature writes it into PushConstants::instance before each draw (see indirectInstance()).
struct IndirectDraw
{
    uint instance;      ///< The instance's index in the InstanceData array, for D3D12.
    uint vertexCount;   ///< Vertices.
    uint instanceCount; ///< Instances; 1.
    uint firstVertex;   ///< SV_VertexID of the first vertex: the mesh's first index.
    uint firstInstance; ///< The instance's index again, for Vulkan, where SV_InstanceID includes it.
    uint padding0;      ///< Pads to 32 bytes.
    uint padding1;      ///< Pads to 32 bytes.
    uint padding2;      ///< Pads to 32 bytes.
};

/// One vertex, pulled by the vertex shader. Encoded by Math::quantizePosition() and Math::packOctahedral().
struct PackedVertex
{
    uint xy;     ///< x and y as UNORM16, low half first, across the mesh's bounds.
    uint z;      ///< z as UNORM16.
    uint normal; ///< Octahedral normal.
    uint uv;     ///< Texture coordinates as two half floats, u in the low half.
};

/// Bits of MaterialData::flags.
#define MEOW2_MATERIAL_ALPHA_MASK 1u ///< Discard pixels whose alpha is below the cutoff.

/// One material, in a buffer shaders read by index. Linear colours.
struct MaterialData
{
    float4 baseColorFactor;  ///< Multiplies the base colour texture; RGBA.
    float4 parameters;       ///< x: alpha cutoff; y: metallic; z: roughness; w: normal scale.
    uint baseColorTexture;   ///< Heap index of an sRGB Texture2D, or 0 (no texture: white).
    uint sampler;            ///< Sampler heap index.
    uint flags;              ///< MEOW2_MATERIAL_* bits.
    uint padding0;           ///< Pads to 16 bytes.
};

/// One mesh, in a buffer shaders read by index.
struct MeshDraw
{
    uint vertexBuffer;    ///< Heap index of its PackedVertex array.
    uint indexBuffer;     ///< Heap index of its uint indices.
    uint firstIndex;      ///< Where its indices start.
    uint indexCount;      ///< How many.
    float4 boundsCenter;  ///< xyz: the centre of its box; w: the radius of a sphere around it.
    float4 boundsExtents; ///< xyz: half the box's size; w: unused.
};

/// What the culling pass reads, once per frame.
struct CullConstants
{
    float4 plane0;       ///< Frustum plane, positive inside.
    float4 plane1;       ///< Frustum plane.
    float4 plane2;       ///< Frustum plane.
    float4 plane3;       ///< Frustum plane.
    float4 plane4;       ///< Frustum plane.
    float4 plane5;       ///< Frustum plane.
    uint instanceBuffer; ///< Heap index of the InstanceData array.
    uint meshBuffer;     ///< Heap index of the MeshDraw array.
    uint drawBuffer;     ///< Heap index of the IndirectDraw array to fill.
    uint countBuffer;    ///< Heap index of the draw count.
    uint instanceCount;  ///< How many instances to test.
    uint padding0;       ///< Pads to 16 bytes.
    uint padding1;       ///< Pads to 16 bytes.
    uint padding2;       ///< Pads to 16 bytes.
};

#endif
