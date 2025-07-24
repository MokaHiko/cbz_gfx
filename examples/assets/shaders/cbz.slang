module cbz;

namespace cbz {
public const static int BUFFER0 = 0;
public const static int BUFFER1 = 1;

public const static int GLOBAL_TRANSFORM_BUFFER = 3;

public const static int TEXTURE0 = 4;
public const static int TEXTURE1 = 6;

public static const float32_t PI = 3.14159265359;

// Uniforms
public typedef float4 Vec4;
public typedef uint4 UVec4;
public typedef int4 IVec4;

static uint32_t sRandState;
static bool sFirstRandom = true;

public float32_t rngNextFloat() {
  uint32_t rand = rngNextInt();
  return float32_t(rand) / 4294967296.0f; // Use 'f' for float32
}

// @returns random uint32_t unique to this frame.
uint32_t rngNextInt() {
  // PCG random number generator
  let newState = sRandState * 747796405u + 2891336453u;
  sRandState = newState;

  let word = ((newState >> ((newState >> 28u) + 4u)) ^ newState) * 277803737u;
  return (word >> 22u) ^ word;
}

public void rngInit(float2 pixel, uint2 resolution, uint32_t frame) {
  // Convert float2 pixel to uint2 for hashing
  uint2 ipixel = uint2(pixel);
  uint32_t base = ipixel.x + ipixel.y * resolution.x;
  uint32_t hashedFrame = jenkinsHash(frame);

  sRandState = jenkinsHash(base ^ hashedFrame);
  sFirstRandom = false;
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

public struct Buffer<T> {
  private StructuredBuffer<T> buffer;

  // Overload `[]` operator.
  public __subscript(int i)->T {
    get { return buffer[i]; }
  }
};

public struct DynamicBuffer<T> {
  private RWStructuredBuffer<T> buffer;

  // Overload `[]` operator.
  public __subscript(int i)->T {
    get { return buffer[i]; }
    set { buffer[i] = newValue;}
  }
};

public struct SampledTexture {
  public float4 sample(float2 uv) { return mTexture.Sample(mSampler, uv); };

  private Texture2D mTexture;
  private SamplerState mSampler;
};

} // namespace cbz