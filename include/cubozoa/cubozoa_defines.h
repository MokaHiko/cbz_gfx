#ifndef CBZ_DEFINES_H_
#define CBZ_DEFINES_H_

#include <cstdint>

namespace cbz {

enum class NetworkStatus {
  eNone,
  eHost,
  eClient,
};

// Minimum renderer limits
constexpr uint32_t MAX_TARGETS = 128;
static_assert(MAX_TARGETS <= std::numeric_limits<uint32_t>::max(),
              "MAX_TARGETS must fit in a uint32_t");

constexpr uint32_t MAX_COMMAND_SUBMISSIONS = 128;
static_assert(MAX_COMMAND_SUBMISSIONS <= std::numeric_limits<uint32_t>::max(),
              "MAX_DRAW_CALLS must fit in a uint32_t");

constexpr uint32_t MAX_COMMAND_TEXTURES = 32;
static_assert(MAX_COMMAND_TEXTURES <= std::numeric_limits<uint32_t>::max(),
              "MAX_COMMAND_TEXTURES must fit in a uint32_t");

constexpr uint32_t MAX_COMMAND_BINDINGS = 16;
static_assert(MAX_COMMAND_BINDINGS <= std::numeric_limits<uint32_t>::max(),
              "MAX_COMMAND_BINDINGS must fit in a uint32_t");

// @ note one to one mapping with 'WGPUVertexFormat'
enum class VertexFormat : uint32_t {
  eUndefined = 0x00000000,
  eUint8x2 = 0x00000001,
  eUint8x4 = 0x00000002,
  eSint8x2 = 0x00000003,
  eSint8x4 = 0x00000004,
  eUnorm8x2 = 0x00000005,
  eUnorm8x4 = 0x00000006,
  eSnorm8x2 = 0x00000007,
  eSnorm8x4 = 0x00000008,
  eUint16x2 = 0x00000009,
  eUint16x4 = 0x0000000A,
  eSint16x2 = 0x0000000B,
  eSint16x4 = 0x0000000C,
  eUnorm16x2 = 0x0000000D,
  eUnorm16x4 = 0x0000000E,
  eSnorm16x2 = 0x0000000F,
  eSnorm16x4 = 0x00000010,
  eFloat16x2 = 0x00000011,
  eFloat16x4 = 0x00000012,
  eFloat32 = 0x00000013,
  eFloat32x2 = 0x00000014,
  eFloat32x3 = 0x00000015,
  eFloat32x4 = 0x00000016,
  eUint32 = 0x00000017,
  eUint32x2 = 0x00000018,
  eUint32x3 = 0x00000019,
  eUint32x4 = 0x0000001A,
  eSint32 = 0x0000001B,
  eSint32x2 = 0x0000001C,
  eSint32x3 = 0x0000001D,
  eSint32x4 = 0x0000001E,

  eCount,
};

[[nodiscard]] constexpr uint32_t VertexFormatGetSize(VertexFormat format) {
  switch (format) {
  // 2 bytes
  case VertexFormat::eUint8x2:
  case VertexFormat::eSint8x2:
  case VertexFormat::eUnorm8x2:
  case VertexFormat::eSnorm8x2:
    return 2;

  // 4 bytes
  case VertexFormat::eUint8x4:
  case VertexFormat::eSint8x4:
  case VertexFormat::eUnorm8x4:
  case VertexFormat::eSnorm8x4:
  case VertexFormat::eUint16x2:
  case VertexFormat::eSint16x2:
  case VertexFormat::eUnorm16x2:
  case VertexFormat::eSnorm16x2:
  case VertexFormat::eFloat16x2:
  case VertexFormat::eFloat32:
  case VertexFormat::eUint32:
  case VertexFormat::eSint32:
    return 4;

  // 8 bytes
  case VertexFormat::eUint16x4:
  case VertexFormat::eSint16x4:
  case VertexFormat::eUnorm16x4:
  case VertexFormat::eSnorm16x4:
  case VertexFormat::eFloat16x4:
  case VertexFormat::eFloat32x2:
  case VertexFormat::eUint32x2:
  case VertexFormat::eSint32x2:
    return 8;

  // 12 bytes
  case VertexFormat::eFloat32x3:
  case VertexFormat::eUint32x3:
  case VertexFormat::eSint32x3:
    return 12;

  // 16 bytes
  case VertexFormat::eFloat32x4:
  case VertexFormat::eUint32x4:
  case VertexFormat::eSint32x4:
    return 16;

  default:
    return 0;
  }
}

// @ note one to one mapping with 'WGPUIndexFormat'
enum class IndexFormat : uint32_t {
  eUndefined = 0x00000000,
  eUint16 = 0x00000001,
  eUint32 = 0x00000002,
};

// @note one to one mapping with 'WGPUVertexStepMode'
enum class VertexStepMode : uint32_t {
  eVertex = 0x00000000,
  eInstance = 0x00000001,
  eVertexBufferNotUsed = 0x00000002,
};

// TODO: Removable
enum class VertexAttributeType : uint32_t {
  ePosition = 0,
  eNormal,
  eTexCoord0,
  eColor,
  eTangent,
  eJoints,
  eWeights,

  eCustom,
  eCount,
};

// @note one to one mapping with 'WGPUTextureFormat'
enum class TextureFormat : uint32_t {
  eUndefined = 0x00000000,
  eR8Unorm = 0x00000001,
  eR8Snorm = 0x00000002,
  eR8Uint = 0x00000003,
  eR8Sint = 0x00000004,
  eR16Uint = 0x00000005,
  eR16Sint = 0x00000006,
  eR16Float = 0x00000007,
  eRG8Unorm = 0x00000008,
  eRG8Snorm = 0x00000009,
  eRG8Uint = 0x0000000A,
  eRG8Sint = 0x0000000B,
  eR32Float = 0x0000000C,
  eR32Uint = 0x0000000D,
  eR32Sint = 0x0000000E,
  eRG16Uint = 0x0000000F,
  eRG16Sint = 0x00000010,
  eRG16Float = 0x00000011,
  eRGBA8Unorm = 0x00000012,
  eRGBA8UnormSrgb = 0x00000013,
  eRGBA8Snorm = 0x00000014,
  eRGBA8Uint = 0x00000015,
  eRGBA8Sint = 0x00000016,
  eBGRA8Unorm = 0x00000017,
  eBGRA8UnormSrgb = 0x00000018,
  eRGB10A2Uint = 0x00000019,
  eRGB10A2Unorm = 0x0000001A,
  eRG11B10Ufloat = 0x0000001B,
  eRGB9E5Ufloat = 0x0000001C,
  eRG32Float = 0x0000001D,
  eRG32Uint = 0x0000001E,
  eRG32Sint = 0x0000001F,
  eRGBA16Uint = 0x00000020,
  eRGBA16Sint = 0x00000021,
  eRGBA16Float = 0x00000022,
  eRGBA32Float = 0x00000023,
  eRGBA32Uint = 0x00000024,
  eRGBA32Sint = 0x00000025,
  eStencil8 = 0x00000026,
  eDepth16Unorm = 0x00000027,
  eDepth24Plus = 0x00000028,
  eDepth24PlusStencil8 = 0x00000029,
  eDepth32Float = 0x0000002A,
  eDepth32FloatStencil8 = 0x0000002B,
  eBC1RGBAUnorm = 0x0000002C,
  eBC1RGBAUnormSrgb = 0x0000002D,
  eBC2RGBAUnorm = 0x0000002E,
  eBC2RGBAUnormSrgb = 0x0000002F,
  eBC3RGBAUnorm = 0x00000030,
  eBC3RGBAUnormSrgb = 0x00000031,
  eBC4RUnorm = 0x00000032,
  eBC4RSnorm = 0x00000033,
  eBC5RGUnorm = 0x00000034,
  eBC5RGSnorm = 0x00000035,
  eBC6HRGBUfloat = 0x00000036,
  eBC6HRGBFloat = 0x00000037,
  eBC7RGBAUnorm = 0x00000038,
  eBC7RGBAUnormSrgb = 0x00000039,
  eETC2RGB8Unorm = 0x0000003A,
  eETC2RGB8UnormSrgb = 0x0000003B,
  eETC2RGB8A1Unorm = 0x0000003C,
  eETC2RGB8A1UnormSrgb = 0x0000003D,
  eETC2RGBA8Unorm = 0x0000003E,
  eETC2RGBA8UnormSrgb = 0x0000003F,
  eEACR11Unorm = 0x00000040,
  eEACR11Snorm = 0x00000041,
  eEACRG11Unorm = 0x00000042,
  eEACRG11Snorm = 0x00000043,
  eASTC4x4Unorm = 0x00000044,
  eASTC4x4UnormSrgb = 0x00000045,
  eASTC5x4Unorm = 0x00000046,
  eASTC5x4UnormSrgb = 0x00000047,
  eASTC5x5Unorm = 0x00000048,
  eASTC5x5UnormSrgb = 0x00000049,
  eASTC6x5Unorm = 0x0000004A,
  eASTC6x5UnormSrgb = 0x0000004B,
  eASTC6x6Unorm = 0x0000004C,
  eASTC6x6UnormSrgb = 0x0000004D,
  eASTC8x5Unorm = 0x0000004E,
  eASTC8x5UnormSrgb = 0x0000004F,
  eASTC8x6Unorm = 0x00000050,
  eASTC8x6UnormSrgb = 0x00000051,
  eASTC8x8Unorm = 0x00000052,
  eASTC8x8UnormSrgb = 0x00000053,
  eASTC10x5Unorm = 0x00000054,
  eASTC10x5UnormSrgb = 0x00000055,
  eASTC10x6Unorm = 0x00000056,
  eASTC10x6UnormSrgb = 0x00000057,
  eASTC10x8Unorm = 0x00000058,
  eASTC10x8UnormSrgb = 0x00000059,
  eASTC10x10Unorm = 0x0000005A,
  eASTC10x10UnormSrgb = 0x0000005B,
  eASTC12x10Unorm = 0x0000005C,
  eASTC12x10UnormSrgb = 0x0000005D,
  eASTC12x12Unorm = 0x0000005E,
  eASTC12x12UnormSrgb = 0x0000005F,
};

// @note one to one mapping with 'WGPUAddressMode'
enum class AddressMode : uint32_t {
  eRepeat = 0x00000000,
  eMirrorRepeat = 0x00000001,
  eClampToEdge = 0x00000002,

  eCount,
};

// @note one to one mapping with 'WGPUFilterMode'
enum class FilterMode : uint32_t {
  eNearest = 0x00000000,
  eLinear = 0x00000001,

  eCount,
};

CBZ_API struct TextureBindingDesc {
  FilterMode filterMode;
  AddressMode addressMode;
};

[[nodiscard]] constexpr uint32_t TextureFormatGetSize(TextureFormat format) {
  switch (format) {
  // 1 channel 8-bit
  case TextureFormat::eR8Unorm:
  case TextureFormat::eR8Snorm:
  case TextureFormat::eR8Uint:
  case TextureFormat::eR8Sint:
    return 1;

  // 1 channel 16-bit
  case TextureFormat::eR16Uint:
  case TextureFormat::eR16Sint:
  case TextureFormat::eR16Float:
    return 2;

  // 2 channels 8-bit
  case TextureFormat::eRG8Unorm:
  case TextureFormat::eRG8Snorm:
  case TextureFormat::eRG8Uint:
  case TextureFormat::eRG8Sint:
    return 2;

  // 1 channel 32-bit
  case TextureFormat::eR32Float:
  case TextureFormat::eR32Uint:
  case TextureFormat::eR32Sint:
    return 4;

  // 2 channels 16-bit
  case TextureFormat::eRG16Uint:
  case TextureFormat::eRG16Sint:
  case TextureFormat::eRG16Float:
    return 4;

  // 4 channels 8-bit
  case TextureFormat::eRGBA8Unorm:
  case TextureFormat::eRGBA8UnormSrgb:
  case TextureFormat::eRGBA8Snorm:
  case TextureFormat::eRGBA8Uint:
  case TextureFormat::eRGBA8Sint:
  case TextureFormat::eBGRA8Unorm:
  case TextureFormat::eBGRA8UnormSrgb:
    return 4;

  // Packed formats
  case TextureFormat::eRGB10A2Uint:
  case TextureFormat::eRGB10A2Unorm:
  case TextureFormat::eRG11B10Ufloat:
  case TextureFormat::eRGB9E5Ufloat:
    return 4;

  // 2 channels 32-bit
  case TextureFormat::eRG32Float:
  case TextureFormat::eRG32Uint:
  case TextureFormat::eRG32Sint:
    return 8;

  // 4 channels 16-bit
  case TextureFormat::eRGBA16Uint:
  case TextureFormat::eRGBA16Sint:
  case TextureFormat::eRGBA16Float:
    return 8;

  // 4 channels 32-bit
  case TextureFormat::eRGBA32Float:
  case TextureFormat::eRGBA32Uint:
  case TextureFormat::eRGBA32Sint:
    return 16;

  // Depth/Stencil
  case TextureFormat::eStencil8:
    return 1;
  case TextureFormat::eDepth16Unorm:
    return 2;
  case TextureFormat::eDepth24Plus:
  case TextureFormat::eDepth24PlusStencil8:
    return 4; // Approximation
  case TextureFormat::eDepth32Float:
    return 4;
  case TextureFormat::eDepth32FloatStencil8:
    return 5;

  // Block compressed formats (size per 4x4 block)
  case TextureFormat::eBC1RGBAUnorm:
  case TextureFormat::eBC1RGBAUnormSrgb:
  case TextureFormat::eBC4RUnorm:
  case TextureFormat::eBC4RSnorm:
    return 8;
  case TextureFormat::eBC2RGBAUnorm:
  case TextureFormat::eBC2RGBAUnormSrgb:
  case TextureFormat::eBC3RGBAUnorm:
  case TextureFormat::eBC3RGBAUnormSrgb:
  case TextureFormat::eBC5RGUnorm:
  case TextureFormat::eBC5RGSnorm:
  case TextureFormat::eBC6HRGBUfloat:
  case TextureFormat::eBC6HRGBFloat:
  case TextureFormat::eBC7RGBAUnorm:
  case TextureFormat::eBC7RGBAUnormSrgb:
    return 16;

  default:
    return 0; // Unknown or undefined
  }
}

// @note one to one mapping with 'WGPUTextureDimension'
enum class TextureDimension : uint32_t {
  e1D = 0x00000000,
  e2D = 0x00000001,
  e3D = 0x00000002,
};

enum class UniformType : uint32_t {
  eUINT, // uint32_t
  eVec4, // float[4]
  eMat4, // float[16]
};

enum class TargetType : uint32_t {
  eNone,
  eGraphics,
  eCompute,
};

enum class BufferSlot : uint8_t {
  e0 = 0,
  e1 = 1,
  e2 = 2,
  e3 = 3,

  eCount = 4,
};

enum class TextureSlot : uint8_t {
  e0 = 4,
  e1 = 6,
  e2 = 8,
  e3 = 10,
};

[[nodiscard]] constexpr uint32_t UniformTypeGetSize(UniformType type) {
  switch (type) {
  case UniformType::eUINT:
    return sizeof(uint32_t);
  case UniformType::eVec4:
    return sizeof(float) * 4;
  case UniformType::eMat4:
    return sizeof(float) * 16;
  default:
    return 0;
  }
}

struct VertexAttribute {
  VertexFormat format;
  uint64_t offset;
  uint32_t shaderLocation;
};

class VertexLayout {
public:
  void begin(VertexStepMode mode);
  void push_attribute(VertexAttributeType type, VertexFormat format);
  void end();

  bool operator==(const VertexLayout &other) const;
  bool operator!=(const VertexLayout &other) const;

  std::vector<VertexAttribute> attributes;
  VertexStepMode stepMode;
  uint32_t stride;
};

// @note Handles may be recycled when destroyed
#define CBZ_INVALID_HANDLE ((uint16_t)0xFFFF)
#define CBZ_HANDLE(name)                                                       \
  CBZ_API struct name {                                                        \
    uint16_t idx;                                                              \
  };

CBZ_HANDLE(VertexBufferHandle);
CBZ_HANDLE(IndexBufferHandle);

CBZ_HANDLE(StructuredBufferHandle);
CBZ_HANDLE(TextureHandle);

struct SamplerHandle {
  uint32_t idx;
};

CBZ_HANDLE(UniformHandle);

CBZ_HANDLE(ShaderHandle);
CBZ_HANDLE(GraphicsProgramHandle);
CBZ_HANDLE(ComputeProgramHandle);

}; // namespace cbz

#endif
