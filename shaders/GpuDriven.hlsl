// The GPU-driven path. csCull tests every instance against the frustum and writes a draw for each survivor into one of
// three lists (MEOW2_DRAW_LIST_*). vsMain draws the lists, vsBlend the blended instances the CPU sorted, and psMain
// shades both with metallic-roughness PBR. Indices and vertices are pulled from heap buffers: no vertex input at all.
#include "Common.hlsli"
#include "Atmosphere.hlsli" // c_pi, toneMapNeutral, the sky lookups

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
    const float3 fraction = float3(stored.xy & 0xFFFF, stored.xy >> 16, stored.z & 0xFFFF) / 65535.0;
    return mesh.boundsCenter.xyz - mesh.boundsExtents.xyz + fraction * 2.0 * mesh.boundsExtents.xyz;
}

/// One vertex of an instance, for both entry points.
VertexOutput shadeVertex(uint instanceIndex, uint vertexId)
{
    const FrameConstants frame = frameConstants();
    const InstanceData instance = loadElement<InstanceData>(g_push.instanceBuffer, instanceIndex);
    const MeshDraw mesh = loadElement<MeshDraw>(g_push.userData, instance.mesh);
    const uint index = loadElement<uint>(mesh.indexBuffer, mesh.firstIndex + vertexId);
    const PackedVertex stored = loadElement<PackedVertex>(mesh.vertexBuffer, index);

    VertexOutput output;
    const float4x4 worldMatrix = MEOW2_ROWS(instance, world);
    const float4 world = mul(float4(unpackPosition(stored, mesh), 1.0), worldMatrix);
    output.position = mul(world, MEOW2_ROWS(frame, viewProjection));
    output.worldPosition = world.xyz;

    // Normals go through the inverse transpose, so non-uniform scale doesn't tilt them. For rows a, b, c it's the rows
    // cross(b, c), cross(c, a), cross(a, b) over the determinant; only the determinant's sign matters before normalising,
    // and it keeps a mirrored normal pointing outwards.
    const float3 a = instance.worldRow0.xyz;
    const float3 b = instance.worldRow1.xyz;
    const float3 c = instance.worldRow2.xyz;
    const bool mirrored = (instance.flags & MEOW2_INSTANCE_MIRRORED) != 0;
    const float side = mirrored ? -1.0 : 1.0;
    const float3 normal = unpackOctahedral(stored.normal);
    output.normal = side * (normal.x * cross(b, c) + normal.y * cross(c, a) + normal.z * cross(a, b));
    // Tangents follow the surface like positions do. Mirroring flips the handedness of normal × tangent, so the
    // bitangent's sign flips with it.
    output.tangent.xyz = mul(float4(unpackOctahedral(stored.tangent), 0.0), worldMatrix).xyz;
    output.tangent.w = ((stored.z & 0x80000000u) != 0 ? -1.0 : 1.0) * side;
    output.uv = float2(f16tof32(stored.uv & 0xFFFF), f16tof32(stored.uv >> 16));
    output.color = float4(stored.color & 255, (stored.color >> 8) & 255, (stored.color >> 16) & 255,
                          stored.color >> 24) / 255.0;
    output.material = instance.material;
    output.mirrored = mirrored ? 1 : 0;
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

/// GGX (Trowbridge-Reitz) normal distribution: how many microfacets face along the half vector.
float distributionGgx(float nDotH, float alpha)
{
    const float alpha2 = alpha * alpha;
    const float d = nDotH * nDotH * (alpha2 - 1.0) + 1.0;
    return alpha2 / (c_pi * d * d);
}

/// Height-correlated Smith visibility for GGX, with the 1 / (4 n·l n·v) of the specular term folded in.
float visibilitySmithGgx(float nDotL, float nDotV, float alpha)
{
    const float alpha2 = alpha * alpha;
    const float lightTerm = nDotV * sqrt(nDotL * nDotL * (1.0 - alpha2) + alpha2);
    const float viewTerm = nDotL * sqrt(nDotV * nDotV * (1.0 - alpha2) + alpha2);
    return 0.5 / max(lightTerm + viewTerm, 1e-5);
}

/// Schlick's Fresnel: reflectance rises towards 1 at grazing angles.
float3 fresnelSchlick(float3 f0, float vDotH)
{
    return f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);
}

/// Karis' analytic fit of the split-sum environment BRDF, for the ambient specular without a lookup texture.
float3 environmentBrdf(float3 f0, float nDotV, float roughness)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    const float2 scaleBias = float2(-1.04, 1.04) * a004 + r.zw;
    return f0 * scaleBias.x + scaleBias.y;
}

/// Irradiance from the sky in a normal's direction: its 9 spherical-harmonic coefficients (already convolved with
/// the cosine lobe by Sky.hlsl's csIrradiance) evaluated there.
float3 skyIrradiance(uint buffer, float3 n)
{
    const ByteAddressBuffer coefficients = heapBuffer(buffer);
    const float basis[9] = {0.282095,
                            0.488603 * n.y,
                            0.488603 * n.z,
                            0.488603 * n.x,
                            1.092548 * n.x * n.y,
                            1.092548 * n.y * n.z,
                            0.315392 * (3.0 * n.z * n.z - 1.0),
                            1.092548 * n.x * n.z,
                            0.546274 * (n.x * n.x - n.y * n.y)};
    float3 irradiance = 0.0;
    for (uint k = 0; k < 9; ++k)
    {
        irradiance += coefficients.Load<float4>(k * 16).rgb * basis[k];
    }
    return max(irradiance, 0.0);
}

float4 psMain(VertexOutput input, bool frontFace : SV_IsFrontFace) : SV_Target
{
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

    if (input.material < frame.materialCount)
    {
        const MaterialData material = loadElement<MaterialData>(frame.materialBuffer, input.material);
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

    // Cook-Torrance: Lambert diffuse for the light that enters the surface, GGX specular for what reflects off it.
    // Metals have no diffuse and tint their reflection; dielectrics reflect 4 % at normal incidence.
    roughness = clamp(roughness, 0.045, 1.0); // a perfect mirror would turn the sun into a single bright pixel
    const float alpha = roughness * roughness;
    const float3 diffuseColor = baseColor.rgb * (1.0 - metallic);
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), baseColor.rgb, metallic);

    const float3 view = normalize(frame.cameraPosition.xyz - input.worldPosition);
    const float3 light = frame.sunDirection.xyz;
    const float3 halfway = normalize(view + light);
    const float nDotV = max(dot(normal, view), 1e-4);
    const float nDotL = saturate(dot(normal, light));
    const float nDotH = saturate(dot(normal, halfway));
    const float vDotH = saturate(dot(view, halfway));

    const float3 fresnel = fresnelSchlick(f0, vDotH);
    const float3 specular = fresnel * distributionGgx(nDotH, alpha) * visibilitySmithGgx(nDotL, nDotV, alpha);
    const float3 diffuse = (1.0 - fresnel) * diffuseColor / c_pi;
    float3 sun = frame.sunColor.rgb * frame.sunDirection.w;
    const bool sky = frame.environmentMap != 0;
    if (sky)
    {
        // Sunlight reaching the ground has crossed the atmosphere: reddened and dimmed when the sun is low.
        sun *= sampleTransmittance(frame.transmittanceLut, frame.environmentSampler,
                                   viewHeightFromAltitude(frame.groundColor.w), light.y);
    }
    float3 color = (diffuse + specular) * sun * nDotL;

    // Ambient light from the surroundings. With a sky: image-based lighting, the split-sum approximation (Karis 2013)
    // - diffuse from the sky's spherical harmonics, specular from its environment map blurred for this roughness,
    // weighted by the analytic environment BRDF. Without one: a sky colour above and a ground colour below.
    const float3 reflected = reflect(-view, normal);
    float3 diffuseAmbient;
    float3 specularAmbient;
    if (sky)
    {
        diffuseAmbient = skyIrradiance(frame.irradianceBuffer, normal) / c_pi;
        Texture2D<float4> environment = ResourceDescriptorHeap[frame.environmentMap];
        const float lod = roughness * float(frame.environmentLevels - 1);
        specularAmbient = environment.SampleLevel(heapSampler(frame.environmentSampler),
                                                  octahedralUv(reflected), lod).rgb;
    }
    else
    {
        diffuseAmbient = lerp(frame.groundColor.rgb, frame.skyColor.rgb, normal.y * 0.5 + 0.5);
        specularAmbient = lerp(frame.groundColor.rgb, frame.skyColor.rgb, reflected.y * 0.5 + 0.5);
    }
    color += (diffuseColor * diffuseAmbient + environmentBrdf(f0, nDotV, roughness) * specularAmbient) * occlusion;
    color += emissive;

    // Exposure and the tone curve, in linear light; the target's sRGB view encodes the result.
    color = toneMapNeutral(color * frame.sunColor.w);
    return float4(color, baseColor.a);
}
