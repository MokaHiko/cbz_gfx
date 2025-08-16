#ifndef CBZ_IRENDERER_CONTEXT_H_
#define CBZ_IRENDERER_CONTEXT_H_

#include <cbz_gfx/cbz_gfx_defines.h>

#include <spdlog/spdlog.h>

namespace cbz {

template <typename HandleT> class HandleProvider {
public:
  static inline uint16_t getCount() {
    return static_cast<uint16_t>(sItems.size());
  };

  static inline const std::string &getName(HandleT handle) {
    if (!isValid(handle)) {
      spdlog::error("Attempting to get name of invalid handle!");
      static const std::string invalidName = "<invalid_handle>";
      return invalidName;
    }

    return sItemNames[handle.idx];
  };

  static inline void setName(HandleT handle, const std::string &name) {
    if (!isValid(handle)) {
      spdlog::error("Attempting to get name of invalid handle!");
    }

    sItemNames[handle.idx] = name;
  };

  static inline bool isValid(HandleT handle) {
    if (handle.idx == CBZ_INVALID_HANDLE) {
      return false;
    }

    uint16_t size = getCount();

    if (handle.idx > size) {
      return false;
    }

    return true;
  };

  static void free(HandleT handle) {
    // TODO: Free handles
    if (handle.idx != CBZ_INVALID_HANDLE) {
      sFreeList.push_back(handle);
    }
  };

  static HandleT write(const std::string &name = "") {
    if (sItems.size() >= std::numeric_limits<uint16_t>::max() - 1) {
      return {CBZ_INVALID_HANDLE};
    }

    HandleT newHandle = {getCount()};

    sItems.push_back(newHandle);
    sItemNames.push_back(name);

    return newHandle;
  };

private:
  static inline std::vector<HandleT> sItems;
  static inline std::vector<std::string> sItemNames;
  static inline std::vector<HandleT> sFreeList;
};

[[nodiscard]] constexpr uint32_t UniformTypeGetSize(CBZUniformType type) {
  switch (type) {
  case CBZ_UNIFORM_TYPE_UINT:
    return sizeof(uint32_t);
  case CBZ_UNIFORM_TYPE_VEC4:
    return sizeof(float) * 4;
  case CBZ_UNIFORM_TYPE_MAT4:
    return sizeof(float) * 16;
  default:
    return 0;
  }
}

enum class BindingType {
  eNone,

  eUniformBuffer,
  eSampler,

  eStructuredBuffer,
  eRWStructuredBuffer,

  eTexture2D,
  eTextureCube,
};

struct BindingDesc {
  std::string name;
  BindingType type;

  uint8_t index;

  union {
    uint32_t size;
    uint32_t elementSize;
  };

  uint32_t padding;
};

struct Binding {
  BindingType type;

  union {
    struct {
      CBZUniformType valueType;
      UniformHandle handle;
    } uniformBuffer;

    struct {
      uint32_t slot;
      ImageHandle handle;
    } texture;

    struct {
      uint32_t slot;
      SamplerHandle handle;
    } sampler;

    struct {
      uint32_t slot;
      CBZUniformType valueType;
      StructuredBufferHandle handle;
    } storageBuffer;
  } value;
};

struct ShaderProgramCommand {
  union {
    struct {
      VertexBufferHandle vbh;
      IndexBufferHandle ibh;
      GraphicsProgramHandle ph;
    } graphics;

    struct {
      uint32_t x, y, z; // dispatchSizes
      ComputeProgramHandle ph;
    } compute;
  } program;

  CBZTargetType programType;

  std::vector<Binding> bindings;

  uint64_t sortKey = 0;
  uint32_t submissionID = 0;
  uint8_t target = 0;

  inline uint32_t getDescriptorHash() const { return sortKey & 0xFFFFFFFF; }
};

// @brief A render target represents a framebuffer or a compute pass.
struct RenderTarget {
  std::vector<AttachmentDescription> colorAttachments;
  AttachmentDescription depthAttachment = {{}, ImageHandle{CBZ_INVALID_HANDLE}};
};

class IRendererContext {
public:
  IRendererContext() = default;
  virtual ~IRendererContext() = default;

  virtual Result init(uint32_t width, uint32_t height, void *nsfh,
                      ImageHandle swapchainIMGH) = 0;

  virtual void shutdown() = 0;

  [[nodiscard]] virtual Result
  vertexBufferCreate(VertexBufferHandle vbh, const VertexLayout &vertexLayout,
                     uint32_t count, const void *data = nullptr) = 0;

  virtual void vertexBufferDestroy(VertexBufferHandle vbh) = 0;

  [[nodiscard]] virtual Result
  indexBufferCreate(IndexBufferHandle ibh, CBZIndexFormat format, uint32_t size,
                    const void *data = nullptr) = 0;

  virtual void indexBufferDestroy(IndexBufferHandle ibh) = 0;

  [[nodiscard]] virtual Result
  uniformBufferCreate(UniformHandle uh, CBZUniformType type, uint16_t num,
                      const void *data = nullptr) = 0;

  virtual void uniformBufferUpdate(UniformHandle uh, const void *data,
                                   uint16_t num = 0) = 0;

  virtual void uniformBufferDestroy(UniformHandle uh) = 0;

  [[nodiscard]] virtual Result
  structuredBufferCreate(StructuredBufferHandle sbh, CBZUniformType type,
                         uint32_t elementCount, const void *data,
                         int flags) = 0;

  virtual void structuredBufferUpdate(StructuredBufferHandle sbh,
                                      uint32_t elementCount, const void *data,
                                      uint32_t elementOffset) = 0;

  virtual void structuredBufferDestroy(StructuredBufferHandle sbh) = 0;

  [[nodiscard]] virtual SamplerHandle
  getSampler(TextureBindingDesc texBindingDesc) = 0;

  [[nodiscard]] virtual Result imageCreate(ImageHandle uh,
                                           CBZTextureFormat format, uint32_t w,
                                           uint32_t h, uint32_t depth,
                                           CBZTextureDimension dimension,
                                           CBZImageFlags flags) = 0;

  virtual void imageUpdate(ImageHandle th, void *data, uint32_t count) = 0;

  virtual void imageDestroy(ImageHandle th) = 0;

  [[nodiscard]] virtual Result shaderCreate(ShaderHandle sh,
                                            CBZShaderFlags flags,
                                            const std::string &path) = 0;

  virtual void shaderDestroy(ShaderHandle sh) = 0;

  [[nodiscard]] virtual Result graphicsProgramCreate(GraphicsProgramHandle gph,
                                                     ShaderHandle sh,
                                                     int flags) = 0;
  virtual void graphicsProgramDestroy(GraphicsProgramHandle gph) = 0;

  [[nodiscard]] virtual Result computeProgramCreate(ComputeProgramHandle cph,
                                                    ShaderHandle sh) = 0;

  virtual void
  readBufferAsync(StructuredBufferHandle sbh,
                  std::function<void(const void *data)> callback) = 0;

  virtual void
  textureReadAsync(ImageHandle imgh, const Origin3D *origin,
                   const TextureExtent *extent,
                   std::function<void(const void *data)> callback) = 0;

  virtual void computeProgramDestroy(ComputeProgramHandle cph) = 0;

  virtual uint32_t submitSorted(const std::vector<RenderTarget> &renderTargets,
                                const ShaderProgramCommand *sortedCmds,
                                uint32_t count) = 0;
};

}; // namespace cbz

extern std::unique_ptr<cbz::IRendererContext> RendererContextCreate();

#endif
