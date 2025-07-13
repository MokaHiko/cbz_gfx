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

// uint32_t seed = cbz::initRng(float2(x, y), uint2(width, height), uRaytracingSettings.dim.z);
// const float r = cbz::rngFirstFloat(seed);
// const float g = cbz::rngNextFloat();
// const float b = cbz::rngNextFloat();
// imageBuffer[idx] = float4(r, g, b, 1.0f);
// uint32_t seed = cbz::initRng(float2(x, y), uint2(width, height), uRaytracingSettings.dim.z);
// const float r = cbz::rngFirstFloat(seed);
// const float g = cbz::rngNextFloat();
// const float b = cbz::rngNextFloat();
// imageBuffer[idx] = float4(r, g, b, 1.0f);

} // namespace cbz

static const float3 VOXEL_SIZE = (0.1);
static const float EPSILON = 0.001f;
static const uint32_t MAX_STEPS = 500;
static float2 k = float2(1, -1);
static float h = 0.0001; // replace by an appropriate value

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

    public float3 getOrigin()
    {
        return mOrigin;
    }
    public float3 getDirection()
    {
        return mDir;
    }

    private float3 mOrigin;
    private float3 mDir;
};

public interface SDFShape
{
    public float calcSDF(float3 eye);
    public float3 calcNormal(float3 p);
};

public struct SDFSphere : SDFShape
{
    public __init(float3 position, float radius)
    {
        mPosition[0] = position.x;
        mPosition[1] = position.y;
        mPosition[2] = position.z;
        mRadius = radius;
    }

    // Based on technique by Inigo Quilez (iquilezles.org)
    // See: https://iquilezles.org/articles/distfunctions/ 
    public float calcSDF(float3 eye)
    {
        return length(eye - float3(mPosition[0], mPosition[1], mPosition[2])) - mRadius;
    }

    // Based on technique by Inigo Quilez (iquilezles.org)
    // See: https://iquilezles.org/articles/normalsSDF/ 
    public float3 calcNormal(float3 p) // for function f(p)
    {
        return normalize(k.xyy * calcSDF(p + k.xyy * h) +
                         k.yyx * calcSDF(p + k.yyx * h) +
                         k.yxy * calcSDF(p + k.yxy * h) +
                         k.xxx * calcSDF(p + k.xxx * h));
    }

    // @note: using float[3] instead of float3 to ensure proper 16-byte alignment for WGSL.
    // This avoids backend misalignment issues when targeting WebGPU.
    private float[3] mPosition;
    private float mRadius;
}

// float torusSDF(float3 eye, float2 t)
// {
//     float2 q = float2(length(eye.xz) - t.x, eye.y);
//     return length(q) - t.y;
// }

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

static const SDFShape[2] shapes = {
    SDFSphere(float3(0.0, 0.0, -3.0), 1.0),
    SDFSphere(float3(1.0, 0.0, -1.0), 1.0),
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
    float t = 0.0f;

    float3 pos = ray.getOrigin() / VOXEL_SIZE;
    float3 voxPos = floor(pos);

    float3 stepDir = sign(ray.getDirection());
    float3 delta = VOXEL_SIZE / ray.getDirection(); // How far along the ray we must travel to cross 1 voxel unit in each axis

    float3 maxDist = {
        stepDir.x > 0.0 ? voxPos.x + 1.0 - pos.x : pos.x - voxPos.x,
        stepDir.y > 0.0 ? voxPos.y + 1.0 - pos.y : pos.y - voxPos.y,
        stepDir.z > 0.0 ? voxPos.z + 1.0 - pos.z : pos.z - voxPos.z,
    };

    maxDist *= delta; // Scale voxel distance to world space distance.

    for (int step = 0; step < MAX_STEPS; step++)
    {
        float3 eye = voxPos * VOXEL_SIZE + VOXEL_SIZE * 0.5;

        uint32_t closestShapeIdx = uint32_t::maxValue;
        float shortestDistance = float32_t::maxValue;
        for (uint32_t shapeIdx = 0; shapeIdx < shapes.getCount(); shapeIdx++)
        {
            float sdf = shapes[shapeIdx].calcSDF(eye);

            if (sdf < shortestDistance)
            {
                shortestDistance = sdf;
                closestShapeIdx = shapeIdx;
            }
        }

        // Calculate sdf for all objects and find min
        if (shortestDistance < EPSILON)
        {
            imageBuffer[idx] = float4(shapes[closestShapeIdx].calcNormal(eye), 1.0f);
            return;
        };

        float3 absMaxDist = abs(maxDist);

        if (absMaxDist.x < absMaxDist.y && absMaxDist.x < absMaxDist.z)
        {
            voxPos.x += stepDir.x;
            maxDist.x += delta.x;
        }
        else if (absMaxDist.y < absMaxDist.z)
        {
            voxPos.y += stepDir.y;
            maxDist.y += delta.y;
        }
        else
        {
            voxPos.z += stepDir.z;
            maxDist.z += delta.z;
        }
    }

    float ySNORM = ray.at(1.0f).y * 0.5f + 1.0;
    imageBuffer[idx] = float4((1.0 - ySNORM) * float3(1.0f, 1.0f, 1.0f) + ySNORM * float3(0.5, 0.7, 1.0), 1.0f);
}
