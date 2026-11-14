// The GPU-driven path. csCull tests every instance against the frustum and sorts each survivor into one of three lists
// (MEOW2_DRAW_LIST_*). psMain shades every path with metallic-roughness PBR. Indices and vertices are pulled from heap
// buffers: no vertex input at all.
//
// - Indirect path: the lists hold one IndirectDraw per instance, which vsMain draws; vsBlend draws the blended
//   instances the CPU sorted.
// - Meshlet path (GPUs with mesh shaders): the lists hold TaskItems, up to 32 meshlets of an instance each. csTaskArgs
//   turns their counts into mesh dispatches; asMain culls each meshlet against the frustum and by its normal cone, and
//   msMain draws the ones left.
#include "Common.hlsli"
#include "Lighting.hlsli" // shading, shared with Deferred.hlsl

#ifndef GBUFFER
#define GBUFFER 0
#endif
#if GBUFFER && DEBUG_VIEW != 0
// The G-buffer variant has no debug views: they draw forward. (The cooker still builds every combination of options.)
#undef DEBUG_VIEW
#define DEBUG_VIEW 0
#endif

/// Signed distance from a plane, positive inside.
float planeDistance(float4 plane, float3 position)
{
    return dot(plane.xyz, position) + plane.w;
}

/// Whether a sphere is at least partly inside the frustum.
bool sphereInFrustum(CullConstants cull, float3 center, float radius)
{
    return planeDistance(cull.plane0, center) >= -radius && planeDistance(cull.plane1, center) >= -radius &&
           planeDistance(cull.plane2, center) >= -radius && planeDistance(cull.plane3, center) >= -radius &&
           planeDistance(cull.plane4, center) >= -radius && planeDistance(cull.plane5, center) >= -radius;
}

/// How much an instance's world matrix scales each local axis: the lengths of its first three rows.
float3 axisScales(InstanceData instance)
{
    return float3(length(instance.worldRow0.xyz), length(instance.worldRow1.xyz), length(instance.worldRow2.xyz));
}

/// A local normal in world space: through the inverse transpose, written as the cofactor rows cross(b, c),
/// cross(c, a), cross(a, b) of the rows a, b, c (only the determinant's sign matters before normalising). For a
/// mirrored instance the sign keeps it pointing out of the surface. Not normalised.
float3 worldNormal(InstanceData instance, float3 normal)
{
    const float3 a = instance.worldRow0.xyz;
    const float3 b = instance.worldRow1.xyz;
    const float3 c = instance.worldRow2.xyz;
    const float side = (instance.flags & MEOW2_INSTANCE_MIRRORED) != 0 ? -1.0 : 1.0;
    return side * (normal.x * cross(b, c) + normal.y * cross(c, a) + normal.z * cross(a, b));
}

[numthreads(64, 1, 1)]
void csCull(uint3 id : SV_DispatchThreadID)
{
    const CullConstants cull = loadElement<CullConstants>(g_push.userData, 0);
    if (id.x >= cull.instanceCount)
    {
        return;
    }
    const InstanceData instance = loadElement<InstanceData>(cull.instanceBuffer, id.x);
    const MeshDraw mesh = loadElement<MeshDraw>(cull.meshBuffer, instance.mesh);

    const float3 center = mul(float4(mesh.boundsCenter.xyz, 1.0), MEOW2_ROWS(instance, world)).xyz;
    const float3 scales = axisScales(instance);
    const float radius = mesh.boundsCenter.w * max(scales.x, max(scales.y, scales.z));
    if (!sphereInFrustum(cull, center, radius))
    {
        return;
    }

    // Which list: decided by the material's sides and the instance's winding. Blended ones are drawn by the CPU.
    uint drawList = (instance.flags & MEOW2_INSTANCE_MIRRORED) != 0 ? MEOW2_DRAW_LIST_MIRRORED : MEOW2_DRAW_LIST_BACK;
    if (instance.material < cull.materialCount)
    {
        const MaterialData material = loadElement<MaterialData>(cull.materialBuffer, instance.material);
        if ((material.flags & MEOW2_MATERIAL_BLEND) != 0)
        {
            return;
        }
        if ((material.flags & MEOW2_MATERIAL_DOUBLE_SIDED) != 0)
        {
            drawList = MEOW2_DRAW_LIST_DOUBLE_SIDED;
        }
    }

    if (cull.meshlets != 0)
    {
        // Meshlet path: one TaskItem per 32 meshlets, in consecutive slots taken with one atomic add.
        const uint items = (mesh.meshletCount + MEOW2_TASK_MESHLETS - 1) / MEOW2_TASK_MESHLETS;
        if (items == 0)
        {
            return;
        }
        RWByteAddressBuffer counts = ResourceDescriptorHeap[cull.taskCounts];
        uint first = 0;
        counts.InterlockedAdd((MEOW2_TASK_COUNT_ITEMS + drawList) * 4, items, first);
        if (first + items > cull.taskCapacity)
        {
            return; // can't happen while the capacity counts every instance's items; csTaskArgs clamps the count
        }
        RWByteAddressBuffer tasks = ResourceDescriptorHeap[cull.taskBuffer];
        for (uint k = 0; k < items; ++k)
        {
            TaskItem item;
            item.instance = id.x;
            item.firstMeshlet = k * MEOW2_TASK_MESHLETS;
            item.meshletCount = min(MEOW2_TASK_MESHLETS, mesh.meshletCount - item.firstMeshlet);
            item.padding0 = 0;
            tasks.Store<TaskItem>((drawList * cull.taskCapacity + first + k) * sizeof(TaskItem), item);
        }
        return;
    }

    RWByteAddressBuffer count = ResourceDescriptorHeap[cull.countBuffer];
    uint slot = 0;
    count.InterlockedAdd(drawList * 4, 1, slot);
    if (slot >= cull.drawCapacity)
    {
        return; // can't happen while every list has room for every instance; a guard against writing past the list
    }

    RWByteAddressBuffer draws = ResourceDescriptorHeap[cull.drawBuffer];
    IndirectDraw draw;
    draw.instance = id.x;
    draw.vertexCount = mesh.indexCount;
    draw.instanceCount = 1;
    draw.firstVertex = mesh.firstIndex;
    draw.firstInstance = id.x;
    draw.padding0 = 0;
    draw.padding1 = 0;
    draw.padding2 = 0;
    draws.Store<IndirectDraw>((drawList * cull.drawCapacity + slot) * sizeof(IndirectDraw), draw);
}

/// The meshlet path's dispatches: one task group per TaskItem of each list. Above 65535 groups the dispatch goes 2D,
/// MEOW2_TASK_GROUPS_X wide, which keeps it under the 2^22 groups a dispatch may have in all.
[numthreads(4, 1, 1)]
void csTaskArgs(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= MEOW2_DRAW_LIST_COUNT)
    {
        return;
    }
    const CullConstants cull = loadElement<CullConstants>(g_push.userData, 0);
    RWByteAddressBuffer counts = ResourceDescriptorHeap[cull.taskCounts];
    const uint items = min(min(counts.Load((MEOW2_TASK_COUNT_ITEMS + id.x) * 4), cull.taskCapacity),
                           MEOW2_TASK_GROUPS_X * 128);
    const uint3 groups = items <= 65535 ? uint3(items, 1, 1)
                                        : uint3(MEOW2_TASK_GROUPS_X, (items + MEOW2_TASK_GROUPS_X - 1) / MEOW2_TASK_GROUPS_X, 1);
    RWByteAddressBuffer arguments = ResourceDescriptorHeap[cull.taskArguments];
    arguments.Store4(id.x * 16, uint4(groups, 0));
}

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION0; ///< Camera relative: the camera is at the origin.
    float3 normal : NORMAL0;
    float4 tangent : TANGENT0; ///< xyz along +U; w: the bitangent's sign, already corrected for mirroring.
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation uint material : MATERIAL0;
    nointerpolation uint mirrored : MIRRORED0;
#if DEBUG_VIEW == 1
    nointerpolation uint instance : INSTANCE0; ///< For the instance view.
    nointerpolation uint triangleIndex : TRIANGLE0; ///< For the triangle view: draws are non-indexed, so vertex / 3.
    nointerpolation uint meshlet : MESHLET0; ///< For the meshlet view; c_noMeshlet on the indirect path.
#endif
};

#if DEBUG_VIEW == 1
/// VertexOutput::meshlet of a vertex the indirect path drew: it has no meshlets.
static const uint c_noMeshlet = 0xFFFFFFFFu;
#endif

/// The inverse of Math::packOctahedral.
float3 unpackOctahedral(uint encoded)
{
    const int2 axes = int2(int(encoded << 16) >> 16, int(encoded) >> 16);
    float2 xy = max(float2(axes) / 32767.0, -1.0);
    const float z = 1.0 - abs(xy.x) - abs(xy.y);
    if (z < 0.0)
    {
        const float2 folded = (1.0 - abs(xy.yx)) * float2(xy.x >= 0.0 ? 1.0 : -1.0, xy.y >= 0.0 ? 1.0 : -1.0);
        xy = folded;
    }
    return normalize(float3(xy, z));
}

/// The inverse of Math::quantizePosition.
float3 unpackPosition(PackedVertex stored, MeshDraw mesh)
{
    const float3 fraction = float3(stored.xy & 0xFFFF, stored.xy >> 16, stored.z & 0xFFFF) / 65535.0;
    return mesh.boundsCenter.xyz - mesh.boundsExtents.xyz + fraction * 2.0 * mesh.boundsExtents.xyz;
}

/// One vertex of an instance, by its index in the mesh's vertex buffer: for every path.
VertexOutput shadeStoredVertex(uint instanceIndex, InstanceData instance, MeshDraw mesh, uint index)
{
    const FrameConstants frame = frameConstants();
    const PackedVertex stored = loadElement<PackedVertex>(mesh.vertexBuffer, index);

    VertexOutput output;
    const float4x4 worldMatrix = MEOW2_ROWS(instance, world);
    const float4 world = mul(float4(unpackPosition(stored, mesh), 1.0), worldMatrix);
    output.position = mul(world, MEOW2_ROWS(frame, viewProjection));
    output.worldPosition = world.xyz;

    // Normals go through the inverse transpose (worldNormal()), so non-uniform scale doesn't tilt them.
    const bool mirrored = (instance.flags & MEOW2_INSTANCE_MIRRORED) != 0;
    const float side = mirrored ? -1.0 : 1.0;
    output.normal = worldNormal(instance, unpackOctahedral(stored.normal));
    // Tangents follow the surface like positions do. Mirroring flips the handedness of normal × tangent, so the
    // bitangent's sign flips with it.
    output.tangent.xyz = mul(float4(unpackOctahedral(stored.tangent), 0.0), worldMatrix).xyz;
    output.tangent.w = ((stored.z & 0x80000000u) != 0 ? -1.0 : 1.0) * side;
    output.uv = float2(f16tof32(stored.uv & 0xFFFF), f16tof32(stored.uv >> 16));
    output.color = float4(stored.color & 255, (stored.color >> 8) & 255, (stored.color >> 16) & 255,
                          stored.color >> 24) / 255.0;
    output.material = instance.material;
    output.mirrored = mirrored ? 1 : 0;
#if DEBUG_VIEW == 1
    output.instance = instanceIndex;
    output.triangleIndex = 0;
    output.meshlet = c_noMeshlet;
#endif
    return output;
}

/// One vertex of an instance on the indirect path: draws are non-indexed, so the index comes from the index buffer.
VertexOutput shadeVertex(uint instanceIndex, uint vertexId)
{
    const InstanceData instance = loadElement<InstanceData>(g_push.instanceBuffer, instanceIndex);
    const MeshDraw mesh = loadElement<MeshDraw>(g_push.userData, instance.mesh);
    const uint index = loadElement<uint>(mesh.indexBuffer, mesh.firstIndex + vertexId);
    VertexOutput output = shadeStoredVertex(instanceIndex, instance, mesh, index);
#if DEBUG_VIEW == 1
    // Not SV_PrimitiveID: in a pixel shader it needs geometry-shader support on Vulkan, which many GPUs lack.
    output.triangleIndex = vertexId / 3;
#endif
    return output;
}

/// The indirect lists.
VertexOutput vsMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    return shadeVertex(indirectInstance(instanceId), vertexId);
}

/// Blended instances: one direct draw each, the instance in g_push.instance.
VertexOutput vsBlend(uint vertexId : SV_VertexID)
{
    return shadeVertex(g_push.instance, vertexId);
}

/// What a task group hands its mesh groups: the instance, and its meshlets that survived, one per mesh group.
struct TaskPayload
{
    uint instance;
    uint meshlets[MEOW2_TASK_MESHLETS];
};

groupshared TaskPayload s_payload;
groupshared uint s_visible[MEOW2_TASK_MESHLETS];   ///< 1 for each thread whose meshlet survived.
groupshared uint s_triangles[MEOW2_TASK_MESHLETS]; ///< Its triangles, for the statistics.

/// Whether a meshlet can be seen: its sphere is in the frustum and, unless both sides are drawn, some of its triangles
/// face the camera.
///
/// The cone test is meshoptimizer's: every triangle faces away when the direction from the camera to the sphere's
/// centre lies within the cone's angle of its axis, with the radius as a margin. It needs a camera point (a perspective
/// frustum) and a world matrix that keeps angles: under non-uniform scale or shear the normals' spread changes, so
/// those instances skip it.
bool meshletVisible(CullConstants cull, InstanceData instance, Meshlet meshlet, uint drawList)
{
    const float3 center = mul(float4(meshlet.sphere.xyz, 1.0), MEOW2_ROWS(instance, world)).xyz;
    const float3 scales = axisScales(instance);
    const float largest = max(scales.x, max(scales.y, scales.z));
    const float radius = meshlet.sphere.w * largest;
    if (!sphereInFrustum(cull, center, radius))
    {
        return false;
    }
    const float smallest = min(scales.x, min(scales.y, scales.z));
    if (drawList == MEOW2_DRAW_LIST_DOUBLE_SIDED || cull.origin.w == 0.0 || meshlet.cone.w >= 1.0 ||
        largest > smallest * 1.001)
    {
        return true;
    }
    const float3 axis = normalize(worldNormal(instance, meshlet.cone.xyz));
    const float3 fromCamera = center - cull.origin.xyz;
    return dot(fromCamera, axis) < meshlet.cone.w * length(fromCamera) + radius;
}

/// The meshlet path's task shader: one group per TaskItem, one thread per meshlet. The survivors are packed in their
/// original order (not by an atomic, whose order changes between runs), so a frame draws the same way every time.
/// g_push.instance is the draw list; g_push.userData the MeshDraw array.
[numthreads(MEOW2_TASK_MESHLETS, 1, 1)]
void asMain(uint3 groupId : SV_GroupID, uint thread : SV_GroupIndex)
{
    const FrameConstants frame = frameConstants();
    const uint drawList = g_push.instance;
    RWByteAddressBuffer counts = ResourceDescriptorHeap[frame.taskCounts];
    const uint itemCount = min(counts.Load((MEOW2_TASK_COUNT_ITEMS + drawList) * 4), frame.taskCapacity);
    const uint itemIndex = groupId.y * MEOW2_TASK_GROUPS_X + groupId.x; // 2D past 65535 groups (csTaskArgs)

    bool visible = false;
    uint meshletIndex = 0;
    uint triangles = 0;
    if (itemIndex < itemCount)
    {
        const TaskItem item = loadElement<TaskItem>(frame.taskBuffer, drawList * frame.taskCapacity + itemIndex);
        if (thread == 0)
        {
            s_payload.instance = item.instance;
        }
        if (thread < item.meshletCount)
        {
            const CullConstants cull = loadElement<CullConstants>(frame.cullBuffer, 0);
            const InstanceData instance = loadElement<InstanceData>(g_push.instanceBuffer, item.instance);
            const MeshDraw mesh = loadElement<MeshDraw>(g_push.userData, instance.mesh);
            meshletIndex = item.firstMeshlet + thread;
            const Meshlet meshlet = loadElement<Meshlet>(mesh.meshletBuffer, meshletIndex);
            visible = meshletVisible(cull, instance, meshlet, drawList);
            triangles = meshlet.triangleCount;
        }
    }
    s_visible[thread] = visible ? 1 : 0;
    s_triangles[thread] = visible ? triangles : 0;
    GroupMemoryBarrierWithGroupSync();

    uint slot = 0;
    uint survivors = 0;
    uint drawnTriangles = 0;
    for (uint k = 0; k < MEOW2_TASK_MESHLETS; ++k)
    {
        slot += k < thread ? s_visible[k] : 0;
        survivors += s_visible[k];
        drawnTriangles += s_triangles[k];
    }
    if (visible)
    {
        s_payload.meshlets[slot] = meshletIndex;
    }
    if (thread == 0 && survivors > 0)
    {
        counts.InterlockedAdd(MEOW2_TASK_COUNT_MESHLETS * 4, survivors);
        counts.InterlockedAdd(MEOW2_TASK_COUNT_TRIANGLES * 4, drawnTriangles);
    }
    GroupMemoryBarrierWithGroupSync(); // every slot of the payload is written before it's handed on
    DispatchMesh(survivors, 1, 1, s_payload);
}

/// The meshlet path's mesh shader: one group per meshlet, one thread per vertex and per triangle. Vertices are shaded
/// as vsMain shades them.
[outputtopology("triangle")]
[numthreads(128, 1, 1)]
void msMain(uint thread : SV_GroupThreadID, uint3 groupId : SV_GroupID, in payload TaskPayload payload,
            out vertices VertexOutput outVertices[MEOW2_MESHLET_MAX_VERTICES],
            out indices uint3 outTriangles[MEOW2_MESHLET_MAX_TRIANGLES])
{
    const uint meshletIndex = payload.meshlets[groupId.x];
    const InstanceData instance = loadElement<InstanceData>(g_push.instanceBuffer, payload.instance);
    const MeshDraw mesh = loadElement<MeshDraw>(g_push.userData, instance.mesh);
    const Meshlet meshlet = loadElement<Meshlet>(mesh.meshletBuffer, meshletIndex);
    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);
    if (thread < meshlet.vertexCount)
    {
        const uint index = loadElement<uint>(mesh.meshletVertexBuffer, meshlet.firstVertex + thread);
        VertexOutput output = shadeStoredVertex(payload.instance, instance, mesh, index);
#if DEBUG_VIEW == 1
        output.meshlet = meshletIndex;
#endif
        outVertices[thread] = output;
    }
    if (thread < meshlet.triangleCount)
    {
        const uint packed = loadElement<uint>(mesh.meshletTriangleBuffer, meshlet.firstTriangle + thread);
        outTriangles[thread] = uint3(packed & 255, (packed >> 8) & 255, (packed >> 16) & 255);
    }
}

/// A texture's UV after its KHR_texture_transform, stored as the two rows of a 2 × 3 matrix (MaterialData).
float2 transformUv(float4 row0, float4 row1, float2 uv)
{
    const float3 homogeneous = float3(uv, 1.0);
    return float2(dot(row0.xyz, homogeneous), dot(row1.xyz, homogeneous));
}

/// A texture of a material, or `fallback` when it has none (index 0).
float4 sampleMaterial(uint texture, uint sampler, float2 uv, float4 fallback)
{
    if (texture == 0)
    {
        return fallback;
    }
    // The index differs between draws, so it's marked non-uniform for drivers that need to know.
    Texture2D<float4> image = ResourceDescriptorHeap[NonUniformResourceIndex(texture)];
    return image.Sample(heapSampler(NonUniformResourceIndex(sampler)), uv);
}

#if DEBUG_VIEW == 1
/// A colour from a number, so neighbouring numbers differ (triangles, instances, materials).
float3 hashColor(uint value)
{
    const uint hash = (value + 1u) * 2654435761u;
    return float3(hash & 255, (hash >> 8) & 255, (hash >> 16) & 255) / 255.0;
}
#endif

#if GBUFFER
/// The G-buffer variant's targets (GpuScene's G-buffer textures, in this order).
struct GBufferOutput
{
    float4 albedo : SV_Target0;   ///< rgb: base colour (an sRGB target).
    float4 normal : SV_Target1;   ///< xyz: the shading normal.
    float4 material : SV_Target2; ///< r: metallic, g: roughness, b: occlusion.
    float4 emissive : SV_Target3; ///< rgb: emitted light.
};

GBufferOutput psMain(VertexOutput input, bool frontFace : SV_IsFrontFace)
#else
float4 psMain(VertexOutput input, bool frontFace : SV_IsFrontFace) : SV_Target
#endif
{
#if DEBUG_VIEW == 2
    // The wireframe overlay (a line-fill pipeline): every edge in the bright accent, whatever the material.
    return float4(1.0, 0.353, 0.4, 1.0);
#endif
    const FrameConstants frame = frameConstants();

    float4 baseColor = input.color;
    float metallic = 0.0;
    float roughness = 0.5;
    float occlusion = 1.0;
    float3 emissive = float3(0.0, 0.0, 0.0);
    float3 normal = normalize(input.normal);
    float3 tangent = input.tangent.xyz;
    float bitangentSign = input.tangent.w;
    // Back faces of double-sided surfaces are lit from their own side. A mirrored triangle's front face shows up as a
    // back face to the rasterizer, so the instance's flag is taken into account.
    const bool facing = frontFace != (input.mirrored != 0);
    if (!facing)
    {
        normal = -normal;
        tangent = -tangent;
        bitangentSign = -bitangentSign;
    }

#if DEBUG_VIEW == 1
    const float3 geometricNormal = normal;
    float mipLevel = -1.0; // no base colour texture
#endif
    if (input.material < frame.materialCount)
    {
        const MaterialData material = loadElement<MaterialData>(frame.materialBuffer, input.material);
#if DEBUG_VIEW == 1
        if (material.baseColorTexture != 0)
        {
            const Texture2D<float4> image = ResourceDescriptorHeap[NonUniformResourceIndex(material.baseColorTexture)];
            mipLevel = image.CalculateLevelOfDetail(
                heapSampler(NonUniformResourceIndex(material.baseColorSampler)),
                transformUv(material.baseColorUv0, material.baseColorUv1, input.uv));
        }
#endif
        baseColor *= material.baseColorFactor *
                     sampleMaterial(material.baseColorTexture, material.baseColorSampler,
                                    transformUv(material.baseColorUv0, material.baseColorUv1, input.uv), float4(1, 1, 1, 1));
        if ((material.flags & MEOW2_MATERIAL_ALPHA_MASK) != 0 && baseColor.a < material.parameters.x)
        {
            discard;
        }
        // glTF packs roughness in G and metallic in B.
        const float4 metalRough = sampleMaterial(
            material.metallicRoughnessTexture, material.metallicRoughnessSampler,
            transformUv(material.metallicRoughnessUv0, material.metallicRoughnessUv1, input.uv), float4(1, 1, 1, 1));
        metallic = material.parameters.y * metalRough.b;
        roughness = material.parameters.z * metalRough.g;
        const float ao = sampleMaterial(material.occlusionTexture, material.occlusionSampler,
                                        transformUv(material.occlusionUv0, material.occlusionUv1, input.uv),
                                        float4(1, 1, 1, 1)).r;
        occlusion = 1.0 + material.emissiveFactor.w * (ao - 1.0);
        emissive = material.emissiveFactor.rgb *
                   sampleMaterial(material.emissiveTexture, material.emissiveSampler,
                                  transformUv(material.emissiveUv0, material.emissiveUv1, input.uv), float4(1, 1, 1, 1)).rgb;
        if (material.normalTexture != 0)
        {
            // Tangent space: X along +U, Y along the bitangent (towards decreasing V, glTF's +Y up), Z the normal.
            // Only X and Y are read: BC5 normal maps keep just those two, so Z is rebuilt from them (a unit vector
            // facing out of the surface). For uncompressed maps it's the same Z, up to rounding.
            float3 local;
            local.xy = sampleMaterial(material.normalTexture, material.normalSampler,
                                      transformUv(material.normalUv0, material.normalUv1, input.uv), float4(0.5, 0.5, 1, 1))
                           .xy * 2.0 - 1.0;
            local.z = sqrt(saturate(1.0 - dot(local.xy, local.xy)));
            local.xy *= material.parameters.w;
            tangent = normalize(tangent - normal * dot(normal, tangent)); // re-orthogonalised after interpolation
            const float3 bitangent = cross(normal, tangent) * bitangentSign;
            normal = normalize(local.x * tangent + local.y * bitangent + local.z * normal);
        }
        if ((material.flags & MEOW2_MATERIAL_BLEND) == 0)
        {
            baseColor.a = 1.0;
        }
    }
    else
    {
        // No material (test scenes of many cubes): a colour from the index, so neighbours differ.
        const uint hash = input.material * 2654435761u;
        baseColor = float4(float3(hash & 255, (hash >> 8) & 255, (hash >> 16) & 255) / 255.0, 1.0);
    }

    roughness = clamp(roughness, 0.045, 1.0); // a perfect mirror would turn the sun into a single bright pixel
#if GBUFFER
    // Deferred: the surface goes into the G-buffer, and Deferred.hlsl's psLighting shades it with the same
    // lightSurface(). No position: lighting rebuilds it from depth.
    GBufferOutput gbuffer;
    gbuffer.albedo = float4(baseColor.rgb, 1.0);
    gbuffer.normal = float4(normal, 1.0);
    gbuffer.material = float4(metallic, roughness, occlusion, 1.0);
    gbuffer.emissive = float4(emissive, 1.0);
    return gbuffer;
#else
    Surface surface;
    surface.baseColor = baseColor.rgb;
    surface.metallic = metallic;
    surface.roughness = roughness;
    surface.occlusion = occlusion;
    surface.emissive = emissive;
    surface.normal = normal;
    surface.position = input.worldPosition;
    const LightTerms terms = lightSurface(frame, surface);
    const float3 view = terms.view;
    const float3 color = (terms.sunDiffuse + terms.sunSpecular + terms.ambientDiffuse + terms.ambientSpecular + emissive) *
                         frame.sunColor.w; // exposure, in linear light: the tone curve is its own pass (ToneMap.hlsl)
#if DEBUG_VIEW == 1
    // Debug views: one property instead of the shaded colour. Written as display values; the debug tone-map pass shows
    // them without the tone curve. Lighting views keep the exposure, like the scene.
    const float3 white = float3(1.0, 1.0, 1.0);
    switch (frame.debugView)
    {
        case MEOW2_DEBUG_VIEW_TRIANGLES:
            return float4(hashColor(input.triangleIndex) * (0.6 + 0.4 * saturate(dot(normal, view))), 1.0);
        case MEOW2_DEBUG_VIEW_INSTANCES:
            return float4(hashColor(input.instance), 1.0);
        case MEOW2_DEBUG_VIEW_MESHLETS:
            if (input.meshlet == c_noMeshlet)
            {
                return float4(0.25, 0.25, 0.25, 1.0); // drawn by the indirect path: no meshlets
            }
            // Mixed with the instance, so two copies of a mesh side by side don't look the same.
            return float4(hashColor(input.meshlet ^ (input.instance * 7919u)) * (0.6 + 0.4 * saturate(dot(normal, view))),
                          1.0);
        case MEOW2_DEBUG_VIEW_MATERIALS:
            return float4(hashColor(input.material), 1.0);
        case MEOW2_DEBUG_VIEW_OVERDRAW:
            return float4(0.0625, 0.0625, 0.0625, 1.0); // 1/16 per layer, added up by the additive pipelines
        case MEOW2_DEBUG_VIEW_DEPTH:
        {
            // Reversed infinite Z: depth = near / distance, and the projection's [3][2] is near.
            const float nearZ = frame.projectionRow3.z;
            const float distance = nearZ / max(input.position.z, 1e-7);
            const float shade = 1.0 - saturate(log2(max(distance / nearZ, 1.0)) / 16.0);
            return float4(shade, shade, shade, 1.0);
        }
        case MEOW2_DEBUG_VIEW_GEOMETRY_NORMAL:
            return float4(geometricNormal * 0.5 + 0.5, 1.0);
        case MEOW2_DEBUG_VIEW_SHADING_NORMAL:
            return float4(normal * 0.5 + 0.5, 1.0);
        case MEOW2_DEBUG_VIEW_TANGENT:
            return float4(normalize(tangent) * 0.5 + 0.5, 1.0);
        case MEOW2_DEBUG_VIEW_UV:
            return float4(frac(input.uv), 0.0, 1.0);
        case MEOW2_DEBUG_VIEW_MIP_LEVEL:
        {
            if (mipLevel < 0.0)
            {
                return float4(0.25, 0.25, 0.25, 1.0); // no texture
            }
            const float3 colors[6] = {float3(1, 0, 0), float3(1, 1, 0), float3(0, 1, 0), float3(0, 1, 1), float3(0, 0, 1),
                                      float3(1, 0, 1)};
            const uint level = min(uint(mipLevel), 4u);
            return float4(lerp(colors[level], colors[level + 1], frac(mipLevel)), 1.0);
        }
        case MEOW2_DEBUG_VIEW_BASE_COLOR:
            return float4(baseColor.rgb, 1.0);
        case MEOW2_DEBUG_VIEW_METALLIC:
            return float4(white * metallic, 1.0);
        case MEOW2_DEBUG_VIEW_ROUGHNESS:
            return float4(white * roughness, 1.0);
        case MEOW2_DEBUG_VIEW_OCCLUSION:
            return float4(white * occlusion, 1.0);
        case MEOW2_DEBUG_VIEW_EMISSIVE:
            return float4(emissive, 1.0);
        case MEOW2_DEBUG_VIEW_ALPHA:
            return float4(white * baseColor.a, 1.0);
        case MEOW2_DEBUG_VIEW_LIGHTING:
        {
            // A white dielectric under the same light: diffuse plus a 4 % specular.
            const float3 whiteFresnel = fresnelSchlick(float3(0.04, 0.04, 0.04), terms.vDotH);
            const float3 lit = ((1.0 - whiteFresnel) / c_pi + whiteFresnel * distributionGgx(terms.nDotH, terms.alpha) *
                                                                  visibilitySmithGgx(terms.nDotL, terms.nDotV, terms.alpha)) *
                                   terms.sun * terms.nDotL +
                               terms.skyDiffuse * occlusion;
            return float4(lit * frame.sunColor.w, 1.0);
        }
        case MEOW2_DEBUG_VIEW_DIFFUSE:
            return float4((terms.sunDiffuse + terms.ambientDiffuse) * frame.sunColor.w, 1.0);
        case MEOW2_DEBUG_VIEW_SPECULAR:
            return float4((terms.sunSpecular + terms.ambientSpecular) * frame.sunColor.w, 1.0);
        case MEOW2_DEBUG_VIEW_BLACK:
            return float4(0.0, 0.0, 0.0, 1.0);
        default:
            break;
    }
#endif
    return float4(color, baseColor.a);
#endif
}
