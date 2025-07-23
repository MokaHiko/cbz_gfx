namespace cbz
{
#define CBZ_DECLARE_BUFFER_0 [vk::binding(0)]
#define CBZ_DECLARE_BUFFER_1 [vk::binding(1)]

#define CBZ_DECLARE_TEXTURE_1 [vk::binding(4)]
#define CBZ_DECLARE_TEXTURE_2 [vk::binding(6)]
public static const float32_t PI = 3.14159265359;

// Uniforms
public typedef float4 Vec4;
public typedef uint4 UVec4;
public typedef int4 IVec4;

static uint32_t sRandState;
static bool sFirstRandom = true;

public float32_t rngNextFloat()
{
    uint32_t rand = rngNextInt();
    return float32_t(rand) / 4294967296.0f; // Use 'f' for float32
}

// @returns random uint32_t unique to this frame.
uint32_t rngNextInt()
{
    // PCG random number generator
    let newState = sRandState * 747796405u + 2891336453u;
    sRandState = newState;

    let word = ((newState >> ((newState >> 28u) + 4u)) ^ newState) * 277803737u;
    return (word >> 22u) ^ word;
}

public void rngInit(float2 pixel, uint2 resolution, uint32_t frame)
{
    // Convert float2 pixel to uint2 for hashing
    uint2 ipixel = uint2(pixel);
    uint32_t base = ipixel.x + ipixel.y * resolution.x;
    uint32_t hashedFrame = jenkinsHash(frame);

    sRandState = jenkinsHash(base ^ hashedFrame);
    sFirstRandom = false;
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

    // @returns the normal of a shape at the point 'p' normalized.
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

public struct SDFTorus : SDFShape
{
    public __init(float3 position, float minor, float major)
    {
        mPosition[0] = position.x;
        mPosition[1] = position.y;
        mPosition[2] = position.z;

        mMinor = minor;
        mMajor = major;
    }

    // Based on technique by Inigo Quilez (iquilezles.org)
    // See: https://iquilezles.org/articles/distfunctions/
    public float calcSDF(float3 eye)
    {
        float3 p = eye - float3(mPosition[0], mPosition[1], mPosition[2]);
        float2 q = float2(length(p.xz) - mMinor, p.y);
        return length(q) - mMajor;
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
    private float mMinor;
    private float mMajor;
}

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

// Based on technique by Nelari (nelair.us)
// See: https:// nelari.us/post/weekend_raytracing_with_wgpu_1/#generate-random-points-in-spheres-and-disks
float3 rngNextVec3InUnitSphere()
{
    // r^3 ~ U(0, 1)
    let r = pow(cbz::rngNextFloat(), 0.33333f);
    let cosTheta = 1.0f - 2.0f * cbz::rngNextFloat();
    let sinTheta = sqrt(1.0f - cosTheta * cosTheta);
    let phi = 2.0f * cbz::PI * cbz::rngNextFloat();

    let x = r * sinTheta * cos(phi);
    let y = r * sinTheta * sin(phi);
    let z = cosTheta;

    return float3(x, y, z);
}

float3 rngNextVec3InUnitHemisphere(float3 normal)
{
    float3 unitOnSphere = rngNextVec3InUnitSphere();
    float factor = dot(normal, unitOnSphere);
    return unitOnSphere * sign(factor);
}

public struct RayHit
{
    Ray nextRay;
    float3 color;
    bool shouldContinue;
}

public interface IMaterial
{
    public RayHit scatter(float3 p, float3 normal, Ray ray);
};

public struct LambertianMaterial : IMaterial
{
    __init(float3 albedo)
    {
        mAlbedo[0] = albedo.x;
        mAlbedo[1] = albedo.y;
        mAlbedo[2] = albedo.z;
    }

    public RayHit scatter(float3 p, float3 normal, Ray ray)
    {
        // Diffuse
        RayHit hit;

        float3 direction = normal + rngNextVec3InUnitSphere();
        if (length(direction) <= 0.001f)
        {
            direction = normal;
        }

        hit.nextRay = Ray(float3(p + normal * EPSILON), normal + rngNextVec3InUnitSphere());
        hit.color = float3(mAlbedo[0], mAlbedo[1], mAlbedo[2]);
        hit.shouldContinue = true;

        return hit;
    }

    private float[3] mAlbedo;
    private float _padding = 0.0f; // Ensure 16-byte alignment
};

public struct MetallicMaterial : IMaterial
{
    __init(float3 albedo)
    {
        mAlbedo[0] = albedo.x;
        mAlbedo[1] = albedo.y;
        mAlbedo[2] = albedo.z;
    }

    public RayHit scatter(float3 p, float3 normal, Ray ray)
    {
        // Reflect
        RayHit hit;

        hit.nextRay = Ray(float3(p + normal * EPSILON), reflect(ray.getDirection(), normal));
        hit.color = float3(mAlbedo[0], mAlbedo[1], mAlbedo[2]);
        hit.shouldContinue = true;

        return hit;
    }

    private float[3] mAlbedo;
    private float _padding = 0.0f; // Ensure 16-byte alignment
};

public struct DielectricMaterial : IMaterial
{
    __init(float refractiveIndex)
    {
        mRefractiveIndex = refractiveIndex;
    }

    public RayHit scatter(float3 p, float3 normal, Ray ray)
    {
        RayHit hit;

        // Determine if the ray is inside the object
        bool isInside = dot(ray.getDirection(), normal) > 0.0f;
        float3 outwardNormal = isInside ? -normal : normal;

        float eta = isInside ? 1.0f / mRefractiveIndex : mRefractiveIndex;

        float cosTheta = dot(-ray.getDirection(), outwardNormal);
        float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));

        if (sinTheta * eta > 1.0f)
        {
            // Total internal reflection
            float3 reflectDir = reflect(ray.getDirection(), outwardNormal);
            hit.nextRay = Ray(p + reflectDir * EPSILON, reflectDir);
        }
        else
        {
            // Refraction
            float3 outPerp = eta * (ray.getDirection() + cosTheta * outwardNormal);
            float3 outPar = -sqrt(max(0.0f, 1.0f - dot(outPerp, outPerp))) * outwardNormal;
            float3 refractedDir = outPerp + outPar;

            hit.nextRay = Ray(p + refractedDir * EPSILON, refractedDir);
        }

        hit.color = float3(0.95f); // Slight attenuation
        hit.shouldContinue = true;

        return hit;
    }

    private float mRefractiveIndex;
};

static const IMaterial[] gMaterials = {
    DielectricMaterial(1.33f),
    // LambertianMaterial(float3(0.8f, 0.2f, 0.9f)),

    LambertianMaterial(float3(0.5f, 0.5f, 0.5f)),

    MetallicMaterial(float3(0.9f, 0.9f, 0.9f)),
    LambertianMaterial(float3(1.0f, 0.63f, 0.2f)),
};

static const SDFTorus myTorus = SDFTorus(float3(8.0, 1, 1.0), 2, 1.0);
static const SDFShape[4] gShapes = {
    SDFSphere(float3(4, 1.0, 6.0), 2),
    SDFSphere(float3(0.0, -100.5, -1.0), 100.0),

    SDFSphere(float3(0.0, 3.5, -3.0), 2),

    SDFSphere(float3(-4.0, 1, 1.0), 3)
};

struct RenderableSDF
{
    __init(uint32_t shape, uint32_t material)
    {
        shapeId = shape;
        materialId = material;
    }

    uint32_t shapeId;
    uint32_t materialId;
};

static const RenderableSDF[] renderables = {
    RenderableSDF(0, 0),
    RenderableSDF(1, 1),
    RenderableSDF(2, 2),
    RenderableSDF(3, 3),
    RenderableSDF(4, 3),
};

RayHit Raycast(Ray ray)
{
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

        float shortestDistance = float32_t::maxValue;

        // Renderable
        uint32_t closestShapeIdx = uint32_t::maxValue;
        uint32_t materialIdx = 0;

        for (uint32_t renderableIdx = 0; renderableIdx < renderables.getCount(); renderableIdx++)
        {
            const RenderableSDF rsdf = renderables[renderableIdx];

            float sdf = gShapes[rsdf.shapeId].calcSDF(eye);
            if (rsdf.shapeId == 4)
            {
                sdf = myTorus.calcSDF(eye);
            };

            if (sdf < shortestDistance)
            {
                shortestDistance = sdf;

                closestShapeIdx = rsdf.shapeId;
                materialIdx = rsdf.materialId;
            }
        }

        // Calculate sdf for all objects and find min
        if (shortestDistance < EPSILON)
        {
            float3 normal = gShapes[closestShapeIdx].calcNormal(eye);
            float3 direction = normal + rngNextVec3InUnitSphere();

            RayHit hit;

            // Debug normals
            // hit.shouldContinue = false;
            // hit.color = normal;

            // Debug positions as colors
            // hit.shouldContinue = false;
            // hit.color = normalize(float3(eye + normal * EPSILON));

            // Debug unit hemisphere
            //  hit.color = direction;
            //  hit.shouldContinue = false;

            // hit.nextRay = Ray(float3(eye + normal * EPSILON), direction);
            // hit.color = float3(0.5, 0.5, 0.5);
            // hit.shouldContinue = true;
            // return hit;

            return gMaterials[materialIdx].scatter(eye, normal, ray);
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

    // TODO : Change with light source
    RayHit hit;
    hit.color = float3(1.0f, 1.0f, 1.0f);
    hit.shouldContinue = false;
    return hit;
}

static const float3 VOXEL_SIZE = (1.0);
static const float EPSILON = VOXEL_SIZE.x + 0.1;
static const uint32_t MAX_STEPS = 500;
static const uint32_t MAX_BOUNCES = 30;
static float2 k = float2(1, -1);
static float h = 0.0001; // replace by an appropriate value
struct URaytracingSettings
{
    cbz::UVec4 dim; // width, height, frame_counter
};

struct UVoxelVolumeDesc
{
    cbz::IVec4 dim; // (x) width, (y) height, (z) depth
};

struct UCamera
{
    float3 position;

    float3 u;
    float3 v;
    float3 w;

    float3 focal;
};

[CBZ_DECLARE_BUFFER_0]
RWStructuredBuffer<cbz::Vec4> imageBuffer;

// Declare uniforms
ConstantBuffer<URaytracingSettings> uRaytracingSettings;
ConstantBuffer<UCamera> uCamera;

ConstantBuffer<UVoxelVolumeDesc> uVoxelVolumeDesc;

[CBZ_DECLARE_BUFFER_1]
StructuredBuffer<cbz::UVec4> voxelVolume;

RayHit DDATrace(Ray ray)
{
    float3 position = ray.getOrigin() / VOXEL_SIZE;
    float3 voxel = floor(position);

    float3 deltaDist = VOXEL_SIZE / abs(ray.getDirection()); // How far along the ray we must travel to cross 1 voxel unit in each axis

    float3 stepDir = sign(ray.getDirection());

    float3 sideDist = {
        stepDir.x > 0.0 ? voxel.x + 1.0 - position.x : position.x - voxel.x,
        stepDir.y > 0.0 ? voxel.y + 1.0 - position.y : position.y - voxel.y,
        stepDir.z > 0.0 ? voxel.z + 1.0 - position.z : position.z - voxel.z,
    };

    sideDist *= deltaDist; // Scale voxel distance to world space distance.

    for (int _i = 0; _i < MAX_STEPS; _i++)
    {
        float4 conds = step(sideDist.xxyy, sideDist.yzzx);

        // Contains only one field with 1.0
        float3 cases = { 0 };
        cases.x = conds.x * conds.y;                   // if x is <= y && x is <= z
        cases.y = (1.0 - cases.x) * conds.z * conds.w; // if y is <= x && y is <= z
        cases.z = (1.0 - cases.x) * (1.0 - cases.y);   // else case, z is chosen to be the smallest

        voxel += stepDir * cases;

        //Safe version of sideDist += cases * deltaDist that handles NaN and Inf
        sideDist += max((2.0 * cases - 1.0) * deltaDist, 0.0);

        // Only process positive voxels
        uint32_t shouldProcess = 1;
        if ((int32_t)voxel.x < 0 || 
            (int32_t)voxel.y < 0 || 
            (int32_t)voxel.z < 0)
        {
            shouldProcess = 0;
        }

        // Check if we are out of bounds
        if ((int32_t)voxel.x >= uVoxelVolumeDesc.dim.x || 
            (int32_t)voxel.y >= uVoxelVolumeDesc.dim.y || 
            (int32_t)voxel.z >= uVoxelVolumeDesc.dim.z)
        {
            shouldProcess = 0;
        }

        int32_t voxelIndex = int32_t(voxel.x) + int32_t(voxel.y) * uVoxelVolumeDesc.dim.x + int32_t(voxel.z) * uVoxelVolumeDesc.dim.x * uVoxelVolumeDesc.dim.y;
        if (voxelVolume[voxelIndex].w * shouldProcess != 0) // Check if the voxel is empty
        {
            RayHit hit;
            hit.shouldContinue = false;

            float factor = 1.0f / 255.0f;
            //hit.color = float3(voxelVolume[voxelIndex].x * factor, voxelVolume[voxelIndex].y * factor, voxelVolume[voxelIndex].z * factor);
            //hit.color = float3(voxel.x * 0.1f, voxel.y * 0.1f, voxel.z * 0.1f);
            hit.color = float3(1.0f, voxel.y / (float)uVoxelVolumeDesc.dim.y, voxel.x / (float)uVoxelVolumeDesc.dim.x);
            return hit;
        }
    }

    RayHit hit;
    hit.color = float3(1.0f, 1.0f, 1.0f);
    hit.shouldContinue = false;
    return hit;
}

// TODO: Take Control of Graphics & Compute mains
[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint2 gInvocationId: SV_DispatchThreadID)
{
    uint32_t x = gInvocationId.x;
    uint32_t y = gInvocationId.y;

    uint32_t width = uRaytracingSettings.dim.x;
    uint32_t height = uRaytracingSettings.dim.y;

    if (x >= width || y >= height)
    {
        return;
    }

    // Init systems
    cbz::rngInit(float2(x, y), uint2(width, height), uRaytracingSettings.dim.z);

    uint32_t idx = y * width + x;

    float viewPortHeight = 2.0f;
    float viewPortWidth = viewPortHeight * (float(width) / height);

    float3 viewPortU = viewPortWidth * uCamera.u;
    float3 viewPortV = viewPortHeight * uCamera.v * -1.0f;

    float3 pixelDeltaU = viewPortU / width;
    float3 pixelDeltaV = viewPortV / height;

    // Calculate the location of upper left pixel
    float3 viewPort00Pos = uCamera.position + (uCamera.focal.x * uCamera.w) - (viewPortU / 2.0) - (viewPortV / 2.0);
    float3 pixel00Pos = viewPort00Pos + 0.5 * (pixelDeltaU + pixelDeltaV);

    float3 pixelPos = pixel00Pos + (x * pixelDeltaU) + (y * pixelDeltaV);
    float3 rayDir = pixelPos - uCamera.position;

    // Cast rays
    Ray primaryRay = Ray(uCamera.position, rayDir);
    float3 radiance = float3(1.0f);

    RayHit hit;
    hit.shouldContinue = true;
    hit.nextRay = primaryRay;

    uint32_t iter = 0;
    while (iter < MAX_BOUNCES)
    {
        ++iter;

        hit = DDATrace(hit.nextRay);
        radiance *= hit.color;

        if (!hit.shouldContinue)
        {
            // TODO: Make skybox an emitter in raycast
            // float ySNORM = primaryRay.at(1.0f).y * 0.5f + 1.0;
            float ySNORM = Ray(float3(0.0f), rayDir).at(1.0f).y * 0.5f + 1.0;
            float3 skyboxColor = float3((1.0 - ySNORM) * float3(1.0f, 1.0f, 1.0f) + ySNORM * float3(0.5, 0.7, 1.0));

            float3 out = radiance * skyboxColor;

            //Gamma(2) correction
            out = sqrt(out);

            imageBuffer[idx] = float4(out, 1.0f);

            //imageBuffer[idx] = float4(radiance, 1.0f); // Debug only
            return;
        }
    }

    // Exceeded max bounces and no light
    imageBuffer[idx] = float4(0.0f, 0.0f, 0.0f, 1.0f);
}