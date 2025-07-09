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
public float32_t rngFirstFloat(uint32_t seed)
{
    uint32_t rand = rngNextInt(seed);
    return float32_t(rand) / 4294967296.0f; // Use 'f' for float32
}

public float32_t rngNextFloat()
{
    uint32_t rand = rngNextInt(sRandState);
    return float32_t(rand) / 4294967296.0f; // Use 'f' for float32
}

// @returns random uint32_t unique to this frame.
public uint32_t rngNextInt(uint32_t seed)
{
    // PCG random number generator
    let newState = seed * 747796405u + 2891336453u;
    sRandState = newState;

    let word = ((newState >> ((newState >> 28u) + 4u)) ^ newState) * 277803737u;
    return (word >> 22u) ^ word;
}

public uint32_t initRng(float2 pixel, uint2 resolution, uint32_t frame)
{
    // Convert float2 pixel to uint2 for hashing
    uint2 ipixel = uint2(pixel);
    uint32_t base = ipixel.x + ipixel.y * resolution.x;
    uint32_t hashedFrame = jenkinsHash(frame);
    return jenkinsHash(base ^ hashedFrame);
}

uint32_t jenkinsHash(uint32_t input)
{
    var x = input;
    x += x << 10u;
    x ^= x >> 6u;
    x += x << 3u;
    x ^= x >> 11u;
    x += x << 15u;
    return x;
}

} // namespace cbz

public struct Ray
{
    public __init(float3 origin, float3 dir)
    {
        mOrigin = origin;
        mDir = normalize(dir);
    }

    public float3 at(float t)
    {
        return mOrigin + (t * mDir);
    }

    private float3 mOrigin;
    private float3 mDir;
};

// Declare resouces (buffers, textures)
[CBZ_DECLARE_BUFFER_0]
RWStructuredBuffer<cbz::Vec4> imageBuffer;

// Add Uniforms
struct RaytracingSettings
{
    cbz::UVec4 dim; // width, height, frame_counter
};

struct Camera
{
    cbz::Vec4 mSettings; // (x) focal length
    cbz::Vec4 mPosition;

    float3 position()
    {
        return mPosition.xyz;
    }

    float focalLength()
    {
        return mSettings.x;
    }
};

ConstantBuffer<RaytracingSettings> uRaytracingSettings;
ConstantBuffer<Camera> uCamera;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint2 gInvocationId: SV_DispatchThreadID)
{
    uint32_t x = gInvocationId.x;
    uint32_t y = gInvocationId.y;

    uint32_t width = uRaytracingSettings.dim.x;
    uint32_t height = uRaytracingSettings.dim.y;

    if (x >= width || y >= height)
    {
        return;
    }

    uint32_t idx = y * width + x;

    // uint32_t seed = cbz::initRng(float2(x, y), uint2(width, height), uRaytracingSettings.dim.z);
    // const float r = cbz::rngFirstFloat(seed);
    // const float g = cbz::rngNextFloat();
    // const float b = cbz::rngNextFloat();
    // imageBuffer[idx] = float4(r, g, b, 1.0f);

    float viewPortHeight = 2.0f;
    float viewPortWidth = viewPortHeight * (float(width) / height);

    float3 viewPortU = float3(viewPortWidth, 0.0, 0.0);
    float3 viewPortV = float3(0, viewPortHeight, 0.0) * -1.0f;

    float3 pixelDeltaU = viewPortU / width;
    float3 pixelDeltaV = viewPortV / height;

    // Calculate the location of upper left pixel
    float3 viewPort00Pos = uCamera.position() - float3(0.0f, 0.0f, uCamera.focalLength()) - (viewPortU / 2.0) - (viewPortV / 2.0);
    float3 pixel00Pos = viewPort00Pos + 0.5 * (pixelDeltaU + pixelDeltaV);

    float3 pixelPos = pixel00Pos + (x * pixelDeltaU) + (y * pixelDeltaV);
    float3 rayDir = pixelPos - uCamera.position();

    Ray ray = Ray(uCamera.position(), rayDir);

    float ySNORM = ray.at(1.0f).y * 0.5f + 1.0;

    float3 p = uCamera.position();
    const float EPSILON = 0.0001f;
    float s = 1.0f;

    const uint32_t MAX_STEPS = 500;
    for (int i = 0; i < MAX_STEPS; i++) {
        float dist = length(p) - s; // Distance to nearest surface of sphere
        if (dist < EPSILON) {
            //imageBuffer[idx] = float4(1.0f, 0.0f, 0.0f, 1.0f);
            imageBuffer[idx] = float4(rayDir, 1.0f);
            return;
        }; // We've hit something
        p += rayDir * dist; // March forward safely
    }

    imageBuffer[idx] = float4((1.0 - ySNORM) * float3(1.0f, 1.0f, 1.0f) + ySNORM * float3(0.5, 0.7, 1.0), 1.0f); 
}
