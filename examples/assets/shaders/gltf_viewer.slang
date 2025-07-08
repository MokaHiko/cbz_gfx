namespace cbz
{

#define CBZ_DECLARE_TEXTURE_1 [vk::binding(4)]
#define CBZ_DECLARE_TEXTURE_2 [vk::binding(6)]

public struct TransformData
{
    float4x4 model;
    float4x4 view;
    float4x4 proj;

    float4x4 model_inv;
    float4x4 view_inv;
};

[[vk::binding(0)]]
public StructuredBuffer<TransformData> transforms;

public struct SampledTexture {
    public float4 sample(float2 uv) {
        return mTexture.Sample(mSampler, uv);
    };

    Texture2D mTexture;
    SamplerState mSampler;
};

};

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

[CBZ_DECLARE_TEXTURE_1]
cbz::SampledTexture albedo;

[CBZ_DECLARE_TEXTURE_2]
cbz::SampledTexture emissive;

[shader("fragment")]
float4 fragmentMain(VOut in) : SV_Target
{
    return albedo.sample(in.uv) * emissive.sample(in.uv);
}