#ifndef CBZ_IRENDERER_CONTEXT_H_
#define CBZ_IRENDERER_CONTEXT_H_

#include "cubozoa/cubozoa_defines.h"

namespace cbz {

template <typename HandleT> class HandleProvider {
public:
  static inline uint16_t getCount() {
    return static_cast<uint16_t>(sItems.size());
  };

  static inline const std::string &getName(HandleT handle) {
    if (!isValid(handle)) {
      spdlog::error("Attempting to get name of invalid handle!");
    }

    return sItemNames[handle.idx];
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

enum class BindingType {
  eNone,

  eUniformBuffer,
  eSampler,

  eStructuredBuffer,
  eRWStructuredBuffer,

  eTexture2D,
};

struct BindingDesc {
  std::string name;
  BindingType type;

  uint8_t index;

  union {
    uint32_t size;
    uint32_t elementSize;
  };
};

struct Binding {
  BindingType type;

  union {
    struct {
      UniformType valueType;
      UniformHandle handle;
    } uniformBuffer;

    struct {
      uint32_t slot;
      TextureHandle handle;
    } texture;

    struct {
      uint32_t slot;
      SamplerHandle handle;
    } sampler;

    struct {
      uint32_t slot;
      UniformType valueType;
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
  ProgramType programType;

  std::vector<Binding> bindings;

  uint64_t sortKey;
  uint32_t id;

  inline uint32_t getDescriptorHash() const { return sortKey & 0xFFFFFFFF; }
};

class IRendererContext {
public:
  IRendererContext() = default;
  virtual ~IRendererContext() = default;

  virtual Result init(uint32_t width, uint32_t height, void *nsfh) = 0;

  virtual void shutdown() = 0;

  [[nodiscard]] virtual Result
  vertexBufferCreate(VertexBufferHandle vbh, const VertexLayout &vertexLayout,
                     uint32_t count, const void *data = nullptr) = 0;

  virtual void vertexBufferDestroy(VertexBufferHandle vbh) = 0;

  [[nodiscard]] virtual Result
  indexBufferCreate(IndexBufferHandle ibh, IndexFormat format, uint32_t size,
                    const void *data = nullptr) = 0;

  virtual void indexBufferDestroy(IndexBufferHandle ibh) = 0;

  [[nodiscard]] virtual Result
  uniformBufferCreate(UniformHandle uh, UniformType type, uint16_t num,
                      const void *data = nullptr) = 0;

  virtual void uniformBufferUpdate(UniformHandle uh, void *data,
                                   uint32_t num = 0) = 0;

  virtual void uniformBufferDestroy(UniformHandle uh) = 0;

  [[nodiscard]] virtual Result
  structuredBufferCreate(StructuredBufferHandle sbh, UniformType type,
                         uint32_t elementCount, const void *data = nullptr) = 0;

  virtual void structuredBufferUpdate(StructuredBufferHandle sbh,
                                      uint32_t elementCount, const void *data,
                                      uint32_t elementOffset) = 0;

  virtual void structuredBufferDestroy(StructuredBufferHandle sbh) = 0;

  [[nodiscard]] virtual SamplerHandle
  getSampler(TextureBindingDesc texBindingDesc) = 0;

  [[nodiscard]] virtual Result textureCreate(TextureHandle uh,
                                             TextureFormat format, uint32_t x,
                                             uint32_t y, uint32_t z,
                                             TextureDimension dimension) = 0;

  virtual void textureUpdate(TextureHandle th, void *data, uint32_t count) = 0;

  virtual void textureDestroy(TextureHandle th) = 0;

  [[nodiscard]] virtual Result shaderCreate(ShaderHandle sh,
                                            const std::string &path) = 0;
  virtual void shaderDestroy(ShaderHandle sh) = 0;

  [[nodiscard]] virtual Result graphicsProgramCreate(GraphicsProgramHandle gph,
                                                     ShaderHandle sh) = 0;
  virtual void graphicsProgramDestroy(GraphicsProgramHandle gph) = 0;

  [[nodiscard]] virtual Result computeProgramCreate(ComputeProgramHandle cph,
                                                    ShaderHandle sh) = 0;

  virtual void computeProgramDestroy(ComputeProgramHandle cph) = 0;

  virtual void submitSorted(const ShaderProgramCommand *sortedCmds,
                            uint32_t count) = 0;
};

}; // namespace cbz

extern std::unique_ptr<cbz::IRendererContext> RendererContextCreate();

#endif
