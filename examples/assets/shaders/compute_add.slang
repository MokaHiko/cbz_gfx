#define CBZBuffer0 0
#define CBZBuffer1 1
#define CBZBuffer2 2
#define CBZBuffer3 3
#define CBZBuffer4 4

#define CBZTex0 5
#define CBZTex1 6
#define CBZTex2 7
#define CBZTex3 8
#define CBZTex4 9

#define CBZ_STRUCTURED_BUFFER(bindingIdx, T) \
    [[vk::binding(bindingIdx)]] StructuredBuffer<T>

#define CBZ_RW_STRUCTURED_BUFFER(bindingIdx, T) \
    [[vk::binding(bindingIdx)]] RWStructuredBuffer<T>

#define CBZ_SAMPLED_TEXTURE_2D(bindingIdx) \
    [[vk::binding(bindingIdx)]] Sampler2D

#define CBZ_TEXTURE_2D(bindingIdx) \
    [[vk::binding(bindingIdx)]] Texture2D

CBZ_RW_STRUCTURED_BUFFER(CBZBuffer0, float4) bufferA;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint2 dispatchThreadId: SV_DispatchThreadID)
{
    uint idx = dispatchThreadId.x;
    bufferA[idx] = float4(1.0f, 1.0f, 1.0f, 1.0f) * sin(float(idx) / 1000.0 + 10);
}
