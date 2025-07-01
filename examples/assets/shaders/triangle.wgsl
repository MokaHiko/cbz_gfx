struct _MatrixStorage_float4x4std140_0
{
    @align(16) data_0 : array<vec4<f32>, i32(4)>,
};

struct cbz_TransformData_std140_0
{
    @align(16) model_0 : _MatrixStorage_float4x4std140_0,
    @align(16) view_0 : _MatrixStorage_float4x4std140_0,
    @align(16) proj_0 : _MatrixStorage_float4x4std140_0,
    @align(16) model_inv_0 : _MatrixStorage_float4x4std140_0,
    @align(16) view_inv_0 : _MatrixStorage_float4x4std140_0,
};

struct _Array_std140_cbz_TransformData128_0
{
    @align(16) data_1 : array<cbz_TransformData_std140_0, i32(128)>,
};

@binding(0) @group(0) var<uniform> transforms_0 : _Array_std140_cbz_TransformData128_0;
fn cbz_transform_0( drawId_0 : u32) -> mat4x4<f32>
{
    return mat4x4<f32>(transforms_0.data_1[drawId_0].model_0.data_0[i32(0)][i32(0)], transforms_0.data_1[drawId_0].model_0.data_0[i32(0)][i32(1)], transforms_0.data_1[drawId_0].model_0.data_0[i32(0)][i32(2)], transforms_0.data_1[drawId_0].model_0.data_0[i32(0)][i32(3)], transforms_0.data_1[drawId_0].model_0.data_0[i32(1)][i32(0)], transforms_0.data_1[drawId_0].model_0.data_0[i32(1)][i32(1)], transforms_0.data_1[drawId_0].model_0.data_0[i32(1)][i32(2)], transforms_0.data_1[drawId_0].model_0.data_0[i32(1)][i32(3)], transforms_0.data_1[drawId_0].model_0.data_0[i32(2)][i32(0)], transforms_0.data_1[drawId_0].model_0.data_0[i32(2)][i32(1)], transforms_0.data_1[drawId_0].model_0.data_0[i32(2)][i32(2)], transforms_0.data_1[drawId_0].model_0.data_0[i32(2)][i32(3)], transforms_0.data_1[drawId_0].model_0.data_0[i32(3)][i32(0)], transforms_0.data_1[drawId_0].model_0.data_0[i32(3)][i32(1)], transforms_0.data_1[drawId_0].model_0.data_0[i32(3)][i32(2)], transforms_0.data_1[drawId_0].model_0.data_0[i32(3)][i32(3)]);
}

struct VOut_0
{
    @builtin(position) position_0 : vec4<f32>,
    @location(0) color_0 : vec3<f32>,
};

struct vertexInput_0
{
    @location(0) position_1 : vec2<f32>,
    @location(1) color_1 : vec3<f32>,
};

@vertex
fn vertexMain( _S1 : vertexInput_0, @builtin(instance_index) drawID_0 : u32) -> VOut_0
{
    var output_0 : VOut_0;
    output_0.position_0 = (((vec4<f32>(_S1.position_1.xy, 0.0f, 1.0f)) * (cbz_transform_0(drawID_0))));
    output_0.color_0 = _S1.color_1.xyz;
    return output_0;
}

struct pixelOutput_0
{
    @location(0) output_1 : vec4<f32>,
};

struct pixelInput_0
{
    @location(0) color_2 : vec3<f32>,
};

@fragment
fn fragmentMain( _S2 : pixelInput_0, @builtin(position) position_2 : vec4<f32>) -> pixelOutput_0
{
    var _S3 : pixelOutput_0 = pixelOutput_0( vec4<f32>(_S2.color_2, 1.0f) );
    return _S3;
}

