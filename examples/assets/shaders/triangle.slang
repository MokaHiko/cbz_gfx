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

typedef float4 Vec4;

};

#define CBZ_TRANSFORM cbz::transforms[in.drawID].model

public struct Vertex
{
    public float2 position;
    public float3 color;

    uint32_t drawID : SV_VulkanInstanceID;
};

struct VOut
{
    float4 position : SV_Position;
    float3 color;
};

[shader("vertex")]
VOut vertexMain(Vertex in)
{
    VOut output;
    output.position = mul(CBZ_TRANSFORM, float4(in.position.xy, 0.0f, 1.0f));
    output.color = in.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VOut in) : SV_Target
{
    return float4(in.color, 1.0f);
}