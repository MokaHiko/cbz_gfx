import cbz;
import cbz_graphics;

struct Vertex
{
    float3 position;
    float3 normal;
    float2 uv;
};

struct VOut
{
    float4 position : SV_Position;
    float3 normal;
    float2 uv;
};

[shader("vertex")]
VOut vertexMain(Vertex in)
{
    VOut output;
    output.position = float4(in.position.xy, 0.0f, 1.0f);
    output.normal = in.normal;
    output.uv = in.uv;
    return output;
}

[[vk_binding(cbz::BUFFER0)]]
StructuredBuffer<float4> imageBuffer;

[shader("fragment")]
float4 fragmentMain(VOut in) : SV_Target
{
    const uint width = 854;
    const uint height = 480;

    uint32_t x = uint32_t(in.uv.x * float(width));
    uint32_t y = uint32_t(in.uv.y * float(height));
    uint32_t index = y * width + x;

    //return imageBuffer[index] + (0.0000001 * in.position.x * in.normal.x);
    return imageBuffer[index] + (0.0000000000000000001 * in.position.x * in.normal.x);
}