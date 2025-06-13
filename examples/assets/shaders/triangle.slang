struct VOut
{
    float4 position : SV_Position;
}

[shader("vertex")]
VOut vsMain(uint32_t vertexIndex : SV_VertexID) {
    VOut output;

    float2 p = float2(0.0, 0.0);
    if (vertexIndex == 0) {
        p = float2(-0.5, -0.5);
    } else if (vertexIndex == 1) {
        p = float2(0.5, -0.5);
    } else {
        p = float2(0.0, 0.5);
    }

    output.position = float4(p.xy, 0.0f, 1.0f);
    return output;
}

[shader("fragment")]
float4 fsMain(VOut in) : SV_Target {
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
}

