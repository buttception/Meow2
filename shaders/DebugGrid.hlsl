/// Meow2Renderer's infinite grid on the y = 0 plane (never in games), in two steps:
/// - csGrid, a compute shader, works out each pixel of a grid layer (straight-alpha RGBA16F): where the pixel's view ray
///   meets the plane, whether the scene's depth hides that point, and how much grid line covers the pixel there.
/// - psComposite blends the layer over the scene's colour (an alpha-blending pipeline), before tone mapping.
///
/// Two steps rather than writing the scene colour from compute: reading a storage texture with no declared format needs
/// an optional Vulkan feature, and the layer can be inspected like any other texture of the frame.
///
/// g_push.userData is the heap index of a buffer holding one GridConstants record (layout below; written by
/// InfiniteGrid.cpp).

#include "Common.hlsli"

/// The grid's per-frame inputs. 176 bytes.
struct GridConstants
{
    float4 inverseRow0;    ///< Clip space → world: the inverse of the view-projection matrix, four rows.
    float4 inverseRow1;
    float4 inverseRow2;
    float4 inverseRow3;
    float4 viewProjRow0;   ///< World → clip space, four rows.
    float4 viewProjRow1;
    float4 viewProjRow2;
    float4 viewProjRow3;
    float4 camera;         ///< xyz: the camera's position; w: unused.
    uint depthTexture;     ///< Heap index of the scene's depth (reversed Z: 1 near, 0 at infinity).
    uint gridTexture;      ///< Writable heap index of the grid layer.
    uint width;            ///< The layer's size in pixels.
    uint height;
    uint gridRead;         ///< Heap index of the grid layer, to read (D3D12 keeps read and write slots apart).
    uint padding0;
    uint padding1;
    uint padding2;
};

GridConstants loadConstants()
{
    return loadElement<GridConstants>(g_push.userData, 0);
}

/// Where a pixel's view ray meets the plane y = 0. Returns false when it doesn't (looking away, or along it).
bool planeHit(GridConstants grid, float2 pixel, out float3 hit, out float3 direction)
{
    const float2 ndc = float2(pixel.x / float(grid.width) * 2.0 - 1.0, 1.0 - pixel.y / float(grid.height) * 2.0);
    const float4x4 inverse = float4x4(grid.inverseRow0, grid.inverseRow1, grid.inverseRow2, grid.inverseRow3);
    // Two points on the ray: the near plane (depth 1) and twice as far (depth 0.5, reversed infinite Z).
    const float4 nearPoint = mul(float4(ndc, 1.0, 1.0), inverse);
    const float4 farPoint = mul(float4(ndc, 0.5, 1.0), inverse);
    direction = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);
    const float3 origin = grid.camera.xyz;
    hit = float3(0.0, 0.0, 0.0);
    if (abs(direction.y) < 1e-6)
    {
        return false;
    }
    const float distance = -origin.y / direction.y;
    hit = origin + direction * distance;
    return distance > 0.0;
}

/// How much of a pixel the lines of a grid with this cell size cover there: 1 on a line, 0 between them, smooth over
/// one pixel's footprint (`footprint`, the pixel's size on the plane along x and z).
float gridLines(float2 position, float2 footprint, float cell)
{
    const float2 coordinate = position / cell;
    const float2 width = max(footprint / cell, 1e-6);
    const float2 distance = abs(frac(coordinate - 0.5) - 0.5) / width; // in pixels from the nearest line
    // Where cells shrink towards a few pixels the lines would alias into moiré: fade them out, leaving the next
    // larger cell size (or nothing) there.
    const float density = 1.0 - smoothstep(0.1, 0.35, max(width.x, width.y));
    return (1.0 - saturate(min(distance.x, distance.y))) * density;
}

[numthreads(8, 8, 1)]
void csGrid(uint3 id : SV_DispatchThreadID)
{
    const GridConstants grid = loadConstants();
    if (id.x >= grid.width || id.y >= grid.height)
    {
        return;
    }
    RWTexture2D<float4> layer = ResourceDescriptorHeap[grid.gridTexture];
    float3 hit;
    float3 direction;
    if (!planeHit(grid, float2(id.xy) + 0.5, hit, direction))
    {
        layer[id.xy] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Hidden behind the scene: its depth is nearer (larger, reversed Z) than the plane's at this pixel.
    const float4x4 viewProjection = float4x4(grid.viewProjRow0, grid.viewProjRow1, grid.viewProjRow2, grid.viewProjRow3);
    const float4 clip = mul(float4(hit, 1.0), viewProjection);
    const Texture2D<float> depth = ResourceDescriptorHeap[grid.depthTexture];
    if (depth.Load(int3(id.xy, 0)) > clip.z / clip.w)
    {
        layer[id.xy] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // The pixel's footprint on the plane, from where the next pixels' rays land (no derivatives in compute).
    float3 hitRight;
    float3 hitDown;
    float3 unused;
    const bool right = planeHit(grid, float2(id.xy) + float2(1.5, 0.5), hitRight, unused);
    const bool down = planeHit(grid, float2(id.xy) + float2(0.5, 1.5), hitDown, unused);
    const float2 dx = right ? abs(hitRight.xz - hit.xz) : float2(1e3, 1e3);
    const float2 dy = down ? abs(hitDown.xz - hit.xz) : float2(1e3, 1e3);
    const float2 footprint = max(dx, dy);

    // Cell sizes follow the camera's height, in powers of ten, blending into the next as it rises.
    const float height = max(abs(grid.camera.y), 1e-4);
    const float level = log10(height);
    const float minorCell = pow(10.0, floor(level) - 1.0);
    const float blend = frac(level);
    const float minor = gridLines(hit.xz, footprint, minorCell) * (1.0 - blend);
    const float major = gridLines(hit.xz, footprint, minorCell * 10.0);
    float alpha = max(minor * 0.35, major * 0.6);
    float3 colour = float3(0.35, 0.35, 0.35);

    // The axes: X (z = 0) red, Z (x = 0) blue.
    const float xAxis = 1.0 - saturate(abs(hit.z) / max(footprint.y, 1e-6));
    const float zAxis = 1.0 - saturate(abs(hit.x) / max(footprint.x, 1e-6));
    if (xAxis > 0.0)
    {
        colour = lerp(colour, float3(0.8, 0.12, 0.1), xAxis);
        alpha = max(alpha, xAxis * 0.9);
    }
    if (zAxis > 0.0)
    {
        colour = lerp(colour, float3(0.1, 0.25, 0.9), zAxis);
        alpha = max(alpha, zAxis * 0.9);
    }

    // Fades out with distance (in camera heights) and towards the horizon, where lines would alias.
    const float distance = length(hit.xz - grid.camera.xz) / height;
    alpha *= saturate(1.0 - distance / 60.0) * saturate(abs(direction.y) * 10.0);
    layer[id.xy] = float4(colour, alpha);
}

struct CompositeOutput
{
    float4 position : SV_Position;
};

/// One triangle that covers the target.
CompositeOutput vsComposite(uint vertexId : SV_VertexID)
{
    CompositeOutput output;
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(corner * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

/// The layer's texel at this pixel; the pipeline's alpha blending mixes it over the scene.
float4 psComposite(CompositeOutput input) : SV_Target
{
    const GridConstants grid = loadConstants();
    const Texture2D<float4> layer = ResourceDescriptorHeap[grid.gridRead];
    return layer.Load(int3(input.position.xy, 0));
}
