struct VOut_0
{
    @builtin(position) position_0 : vec4<f32>,
};

@vertex
fn vsMain(@builtin(vertex_index) vertexIndex_0 : u32) -> VOut_0
{
    var output_0 : VOut_0;
    var p_0 : vec2<f32>;
    if(vertexIndex_0 == u32(0))
    {
        p_0 = vec2<f32>(-0.5f, -0.5f);
    }
    else
    {
        if(vertexIndex_0 == u32(1))
        {
            p_0 = vec2<f32>(0.5f, -0.5f);
        }
        else
        {
            p_0 = vec2<f32>(0.0f, 0.5f);
        }
    }
    output_0.position_0 = vec4<f32>(p_0.xy, 0.0f, 1.0f);
    return output_0;
}

struct pixelOutput_0
{
    @location(0) output_1 : vec4<f32>,
};

@fragment
fn fsMain(@builtin(position) position_1 : vec4<f32>) -> pixelOutput_0
{
    var _S1 : pixelOutput_0 = pixelOutput_0( vec4<f32>(1.0f, 0.0f, 0.0f, 1.0f) );
    return _S1;
}

