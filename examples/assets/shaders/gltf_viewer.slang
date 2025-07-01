namespace cbz
{

public static const float PI = 3.14159265359;

struct TransformData
{
    float4x4 model;
    float4x4 view;
    float4x4 proj;

    float4x4 model_inv;
    float4x4 view_inv;
};

struct Draw
{
    uint32_t id : CBZSerialize;
};

const ConstantBuffer<TransformData[128]> transforms;

public float4x4 transform(uint32_t drawId)
{
    return transforms[drawId].model;
}

public float4x4 inverse_transform(uint32_t drawId)
{
    return transforms[drawId].model_inv;
}

public float4x4 view(uint32_t drawId)
{
    return transforms[drawId].view;
}

public float4x4 inverse_view(uint32_t drawId)
{
    return transforms[drawId].view_inv;
}

public float4x4 proj(uint32_t drawId)
{
    return transforms[drawId].proj;
}

public float4x4 view_proj(uint32_t drawId)
{
    return mul(proj(drawId), view(drawId));
}

};

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
    output.position = mul(cbz::transform(in.drawID), float4(in.position.xy, 0.0f, 1.0f));
    output.normal = in.normal;
    output.uv = in.uv;
    return output;
}

Texture2D albedo;
SamplerState albedoSampler;

[shader("fragment")]
float4 fragmentMain(VOut in) : SV_Target
{
    return albedo.Sample(albedoSampler, in.uv);
}