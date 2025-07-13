namespace cbz
{

#define CBZ_DECLARE_BUFFER_0 [vk::binding(0)]
#define CBZ_DECLARE_BUFFER_1 [vk::binding(1)]

#define CBZ_DECLARE_TEXTURE_1 [vk::binding(4)]
#define CBZ_DECLARE_TEXTURE_2 [vk::binding(6)]

// Uniforms
typedef float4 Vec4;

// Graphics
public struct TransformData
{
    float4x4 model;
    float4x4 view;
    float4x4 proj;

    float4x4 model_inv;
    float4x4 view_inv;
};

[CBZ_DECLARE_BUFFER_0]
public StructuredBuffer<TransformData> transforms;

public struct SampledTexture {
    public float4 sample(float2 uv) {
        return mTexture.Sample(mSampler, uv);
    };

    Texture2D mTexture;
    SamplerState mSampler;
};

}; // namespace cbz

#define CBZ_TRANSFORM cbz::transforms[in.drawID].model

public struct Vertex
{
    public float3 position;
    public float3 normal;
    public float2 uv;

    uint32_t drawID : SV_VulkanInstanceID;
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
    output.position = mul(CBZ_TRANSFORM, float4(in.position.xy, 0.0f, 1.0f));
    output.normal = in.normal;
    output.uv = in.uv;
    return output;
}

[CBZ_DECLARE_BUFFER_1]
StructuredBuffer<float4> imageBuffer;

[shader("fragment")]
float4 fragmentMain(VOut in) : SV_Target
{
    const uint width = 854;
    const uint height = 480;

    uint32_t x = uint32_t(in.uv.x * float(width));
    uint32_t y = uint32_t(in.uv.y * float(height));
    uint32_t index = y * width + x;

    return imageBuffer[index];
}