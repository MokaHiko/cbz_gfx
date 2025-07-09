namespace cbz
{

#define CBZ_DECLARE_BUFFER_0 [vk::binding(0)]
#define CBZ_DECLARE_BUFFER_1 [vk::binding(1)]

#define CBZ_DECLARE_TEXTURE_1 [vk::binding(4)]
#define CBZ_DECLARE_TEXTURE_2 [vk::binding(6)]

// Uniforms
typedef float4 Vec4;
typedef uint4 UVec4;

static uint32_t sRandState;

//@returns a random float32
public float32_t rngFirstFloat(uint32_t seed) {
    uint32_t rand = rngNextInt(seed);
    return float32_t(rand) / 4294967296.0f; // Use 'f' for float32
}

public float32_t rngNextFloat() {
    uint32_t rand = rngNextInt(sRandState);
    return float32_t(rand) / 4294967296.0f; // Use 'f' for float32
}

// @returns random uint32_t unique to this frame. 
public uint32_t rngNextInt(uint32_t seed) {
    // PCG random number generator
    let newState = seed * 747796405u + 2891336453u;
    sRandState = newState;

    let word = ((newState >> ((newState >> 28u) + 4u)) ^ newState) * 277803737u;
    return (word >> 22u) ^ word;
}

public uint32_t initRng(float2 pixel, uint2 resolution, uint32_t frame) {
    // Convert float2 pixel to uint2 for hashing
    uint2 ipixel = uint2(pixel);
    uint32_t base = ipixel.x + ipixel.y * resolution.x;
    uint32_t hashedFrame = jenkinsHash(frame);
    return jenkinsHash(base ^ hashedFrame);
}

uint32_t jenkinsHash(uint32_t input) {
    var x = input;
    x += x << 10u;
    x ^= x >> 6u;
    x += x << 3u;
    x ^= x >> 11u;
    x += x << 15u;
    return x;
}

} // namespace cbz

// Declare resouces (buffers, textures)
[CBZ_DECLARE_BUFFER_0]
RWStructuredBuffer<cbz::Vec4> imageBuffer;

// Add Uniforms
struct RaytracingSettings{
    cbz::UVec4 dim; // width, height, frame_counter
};

ConstantBuffer<RaytracingSettings> uRaytracingSettings;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint2 gInvocationId: SV_DispatchThreadID)
{
    const uint x = gInvocationId.x;
    const uint y = gInvocationId.y;

    const uint width = uRaytracingSettings.dim.x;
    const uint height = uRaytracingSettings.dim.y;

    if (x >= width || y >= height) {
        return;
    }

    uint idx = y * width + x;

    uint32_t seed = cbz::initRng(float2(x, y), uint2(width, height), uRaytracingSettings.dim.z);
    const float r = cbz::rngFirstFloat(seed);
    const float g = cbz::rngNextFloat();
    const float b = cbz::rngNextFloat();

    imageBuffer[idx] = float4(r, g, b, 1.0f);
    //imageBuffer[idx] = float4(1.0f, 1.0f, 1.0f, 1.0f) * r;
}