/// A triangle with no vertex buffer: the vertex shader builds it from SV_VertexID.
/// The first shader in the repository, and the test case of the build step.

struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VertexOutput vsMain(uint vertexId : SV_VertexID)
{
    const float2 positions[3] = {float2(0.0, 0.5), float2(0.5, -0.5), float2(-0.5, -0.5)};
    const float3 colors[3] = {float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float3(0.0, 0.0, 1.0)};
    VertexOutput output;
    output.position = float4(positions[vertexId], 0.5, 1.0);
    output.color = colors[vertexId];
    return output;
}

float4 psMain(VertexOutput input) : SV_Target
{
#if GRAYSCALE
    const float luma = dot(input.color, float3(0.2126, 0.7152, 0.0722));
    return float4(luma, luma, luma, 1.0);
#else
    return float4(input.color, 1.0);
#endif
}
