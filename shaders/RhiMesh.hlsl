/// Tests of mesh pipelines (RhiDrawTests.cpp). Each mesh group draws one of four columns across the target, column i
/// covering a quarter of its width, in the colour packed into g_push.userData (RGBA8, red in the low byte).
/// - msMain: alone, column = the group.
/// - asMain + msFromTask: the task group k launches one mesh group for column k.
/// - csArgs: writes the group counts (3, 1, 1) into the buffer at heap index g_push.instance, for an indirect dispatch.

#include "Common.hlsli"

struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

/// What a task group passes to its mesh group.
struct Payload
{
    uint column;
};

/// Corner `index` (0..3: x from bit 0, y from bit 1) of a column's quad.
VertexOutput corner(uint column, uint index)
{
    VertexOutput output;
    const float left = -1.0 + float(column) * 0.5;
    output.position = float4(left + float(index & 1) * 0.5, (index & 2) != 0 ? 1.0 : -1.0, 0.5, 1.0);
    const uint packed = g_push.userData;
    output.color = float4(packed & 255, (packed >> 8) & 255, (packed >> 16) & 255, packed >> 24) / 255.0;
    return output;
}

[outputtopology("triangle")]
[numthreads(4, 1, 1)]
void msMain(uint thread : SV_GroupThreadID, uint3 group : SV_GroupID, out vertices VertexOutput outVertices[4],
            out indices uint3 outTriangles[2])
{
    SetMeshOutputCounts(4, 2);
    outVertices[thread] = corner(group.x, thread);
    if (thread < 2)
    {
        outTriangles[thread] = thread == 0 ? uint3(0, 2, 3) : uint3(0, 3, 1);
    }
}

groupshared Payload s_payload;

[numthreads(1, 1, 1)]
void asMain(uint3 group : SV_GroupID)
{
    s_payload.column = group.x;
    DispatchMesh(1, 1, 1, s_payload);
}

[outputtopology("triangle")]
[numthreads(4, 1, 1)]
void msFromTask(uint thread : SV_GroupThreadID, in payload Payload payload, out vertices VertexOutput outVertices[4],
                out indices uint3 outTriangles[2])
{
    SetMeshOutputCounts(4, 2);
    outVertices[thread] = corner(payload.column, thread);
    if (thread < 2)
    {
        outTriangles[thread] = thread == 0 ? uint3(0, 2, 3) : uint3(0, 3, 1);
    }
}

[numthreads(1, 1, 1)]
void csArgs()
{
    RWByteAddressBuffer arguments = ResourceDescriptorHeap[g_push.instance];
    arguments.Store3(0, uint3(3, 1, 1));
}

float4 psMain(VertexOutput input) : SV_Target
{
    return input.color;
}
