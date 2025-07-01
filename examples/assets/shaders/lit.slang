namespace cbz
{

public static const float PI = 3.14159265359;

public struct Vertex
{
    public float3 position : POSITION;
    public float3 normal : NORMAL;
    public float2 uv : TEXCOORD0;
    public float4 color : COLOR;
    public float4 tangent : TANGENT;
};

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

ConstantBuffer<Draw> draw;
StructuredBuffer<TransformData> transforms;

public float4x4 transform()
{
    return transforms[draw.id].model;
}

public float4x4 inverse_transform()
{
    return transforms[draw.id].model_inv;
}

public float4x4 view()
{
    return transforms[draw.id].view;
}

public float4x4 inverse_view()
{
    return transforms[draw.id].view_inv;
}

public float4x4 proj()
{
    return transforms[draw.id].proj;
}

public float4x4 view_proj()
{
    return mul(proj(), view());
}

};

struct Vertex
{
    float2 position;
    float3 color;
};

struct VOut
{
    float4 position : SV_Position;
    float3 color;
};

struct MaterialData {
    float4 color;
    float albedoFactor;
    Texture2D albedo;
    SamplerState abledoSampler;
};

struct CameraData {
    float3 position;
    float exposure;
};

struct SceneData {
    CameraData camera;
    uint32_t dirLightCount;
    uint32_t pointLightCount;
};

ConstantBuffer<MaterialData> materialData;
ConstantBuffer<SceneData> sceneData;

[shader("vertex")]
VOut vertexMain(Vertex in)
{
    VOut output;
    output.position = mul(cbz::transform(), float4(in.position.xy, 0.0f, 1.0f));
    output.color = in.color * sceneData.camera.exposure + output.position.xyz;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VOut in) : SV_Target
{
    return float4(in.color, 1.0f) + float4(materialData.color.xyz, 1.0f);
}