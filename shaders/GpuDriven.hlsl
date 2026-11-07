// The GPU-driven path. csCull tests every instance against the frustum and writes a draw for each survivor. vsMain and
// psMain draw them, pulling indices and vertices from heap buffers, so there's no vertex input at all.
#include "Common.hlsli"

/// Signed distance from a plane, positive inside.
float planeDistance(float4 plane, float3 position)
{
    return dot(plane.xyz, position) + plane.w;
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
    const float scale = sqrt(max(max(dot(instance.worldRow0.xyz, instance.worldRow0.xyz),
                                     dot(instance.worldRow1.xyz, instance.worldRow1.xyz)),
                                 dot(instance.worldRow2.xyz, instance.worldRow2.xyz)));
    const float radius = mesh.boundsCenter.w * scale;
    if (planeDistance(cull.plane0, center) < -radius || planeDistance(cull.plane1, center) < -radius ||
        planeDistance(cull.plane2, center) < -radius || planeDistance(cull.plane3, center) < -radius ||
        planeDistance(cull.plane4, center) < -radius || planeDistance(cull.plane5, center) < -radius)
    {
        return;
    }

    RWByteAddressBuffer count = ResourceDescriptorHeap[cull.countBuffer];
    uint slot = 0;
    count.InterlockedAdd(0, 1, slot);

    RWByteAddressBuffer draws = ResourceDescriptorHeap[cull.drawBuffer];
    IndirectDraw draw;
    draw.vertexCount = mesh.indexCount;
    draw.instanceCount = 1;
    draw.firstVertex = mesh.firstIndex;
    draw.firstInstance = id.x;
    draws.Store<IndirectDraw>(slot * sizeof(IndirectDraw), draw);
}

struct VertexOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL0;
    float3 color : COLOR0;
};

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
    const float3 fraction = float3(stored.xy & 0xFFFF, stored.xy >> 16, stored.z) / 65535.0;
    return mesh.boundsCenter.xyz - mesh.boundsExtents.xyz + fraction * 2.0 * mesh.boundsExtents.xyz;
}

VertexOutput vsMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const FrameConstants frame = frameConstants();
    const InstanceData instance = loadElement<InstanceData>(g_push.instanceBuffer, instanceId);
    const MeshDraw mesh = loadElement<MeshDraw>(g_push.userData, instance.mesh);
    const uint index = loadElement<uint>(mesh.indexBuffer, vertexId);
    const PackedVertex stored = loadElement<PackedVertex>(mesh.vertexBuffer, index);

    VertexOutput output;
    const float4x4 worldMatrix = MEOW2_ROWS(instance, world);
    const float4 world = mul(float4(unpackPosition(stored, mesh), 1.0), worldMatrix);
    output.position = mul(world, MEOW2_ROWS(frame, viewProjection));
    output.normal = mul(float4(unpackOctahedral(stored.normal), 0.0), worldMatrix).xyz;
    const uint hash = instance.material * 2654435761u;
    output.color = float3(hash & 255, (hash >> 8) & 255, (hash >> 16) & 255) / 255.0;
    return output;
}

float4 psMain(VertexOutput input) : SV_Target
{
    const float light = 0.3 + 0.7 * saturate(dot(normalize(input.normal), normalize(float3(0.4, 0.8, -0.5))));
    return float4(input.color * light, 1.0);
}
