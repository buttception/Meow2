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

/// One indirect draw, the layout VkDrawIndirectCommand requires, written by culling.
struct IndirectDraw
{
    uint vertexCount;   ///< Vertices.
    uint instanceCount; ///< Instances; 1.
    uint firstVertex;   ///< SV_VertexID of the first vertex: the mesh's first index.
    uint firstInstance; ///< SV_InstanceID: the instance's index in the InstanceData array.
};

/// One vertex, pulled by the vertex shader. Encoded by Math::quantizePosition() and Math::packOctahedral().
struct PackedVertex
{
    uint xy;     ///< x and y as UNORM16, low half first, across the mesh's bounds.
    uint z;      ///< z as UNORM16.
    uint normal; ///< Octahedral normal.
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
