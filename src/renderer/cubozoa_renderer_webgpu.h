#ifndef CBZ_RENDERER_WGPU_H_
#define CBZ_RENDERER_WGPU_H_

#include "cubozoa/cubozoa_defines.h"
#include "cubozoa_irenderer_context.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef WEBGPU_BACKEND_WGPU
#include <webgpu/wgpu.h>
#endif

#include <glfw3webgpu.h>
#include <webgpu/webgpu.h>

#include <nlohmann/json.hpp>

namespace cbz {

class ShaderWebGPU {
public:
  [[nodiscard]] Result create(const std::string &path);
  void destroy();

  [[nodiscard]] const inline VertexLayout &getVertexLayout() const {
    return mVertexLayout;
  };

  [[nodiscard]] const inline WGPUBindGroupLayout &getBindGroupLayout() const {
    return mBindGroupLayout;
  };

  [[nodiscard]] const inline std::vector<BindingDesc> &getBindings() const {
    return mBindingDescs;
  };

  [[nodiscard]] inline WGPUShaderModule getModule() const { return mModule; };

  [[nodiscard]] inline WGPUShaderStageFlags getShaderStages() const {
    return mStages;
  };

private:
  void parseJsonRecursive(const nlohmann::json &varJson, bool isBinding,
                          uint32_t offsets);

private:
  std::vector<BindingDesc> mBindingDescs;
  WGPUBindGroupLayout mBindGroupLayout;

  VertexLayout mVertexLayout;

  WGPUShaderStageFlags mStages;
  WGPUShaderModule mModule;
};

class VertexBufferWebGPU {
public:
  [[nodiscard]] Result create(const VertexLayout &vertexLayout, uint32_t size,
                              const void *data = nullptr,
                              const std::string &name = "");

  [[nodiscard]] Result bind(WGPURenderPassEncoder renderPassEncoder,
                            uint32_t slot = 0) const;
  void destroy();

  [[nodiscard]] inline uint32_t getVertexCount() const { return mVertexCount; }
  [[nodiscard]] inline const VertexLayout &getVertexLayout() const {
    return mVertexLayout;
  }

private:
  VertexLayout mVertexLayout;
  WGPUBuffer mBuffer;
  uint32_t mVertexCount;
};

class IndexBufferWebGPU {
public:
  [[nodiscard]] Result create(WGPUIndexFormat format, uint32_t count,
                              const void *data = nullptr,
                              const std::string &name = "");

  [[nodiscard]] Result bind(WGPURenderPassEncoder renderPassEncoder) const;

  void destroy();

  [[nodiscard]] inline uint32_t getIndexCount() const { return mIndexCount; }

private:
  WGPUBuffer mBuffer;
  WGPUIndexFormat mFormat;
  uint32_t mIndexCount;
};

class UniformBufferWebWGPU {
public:
  [[nodiscard]] Result create(UniformType type, uint16_t num,
                              const void *data = nullptr,
                              const std::string &name = "");

  void update(const void *data, uint16_t num);

  void destroy();

  [[nodiscard]] inline uint32_t getSize() const {
    return UniformTypeGetSize(mElementType) * mElementCount;
  }

  [[nodiscard]] inline WGPUBindGroupEntry
  createBindGroupEntry(uint32_t binding) const {
    WGPUBindGroupEntry entry = {};
    entry.nextInChain = nullptr;
    entry.binding = binding;
    entry.buffer = mBuffer;
    entry.offset = 0;
    entry.size = getSize();
    return entry;
  }

private:
  WGPUBuffer mBuffer;

  UniformType mElementType;
  uint16_t mElementCount;
};

class StorageBufferWebWGPU {
public:
  [[nodiscard]] Result create(UniformType type, uint16_t num,
                              const void *data = nullptr,
                              const std::string &name = "");

  void update(const void *data, uint16_t num);

  void destroy();

  [[nodiscard]] inline uint32_t getSize() const {
    return UniformTypeGetSize(mElementType) * mElementCount;
  }

  [[nodiscard]] inline WGPUBindGroupEntry
  createBindGroupEntry(uint32_t binding) const {
    WGPUBindGroupEntry entry = {};
    entry.nextInChain = nullptr;
    entry.binding = binding;
    entry.buffer = mBuffer;
    entry.offset = 0;
    entry.size = getSize();
    return entry;
  }

private:
  WGPUBuffer mBuffer;

  UniformType mElementType;
  uint16_t mElementCount;
};

class TextureWebGPU {
public:
  Result create(uint32_t w, uint32_t h, uint32_t depth,
                WGPUTextureDimension dimension, WGPUTextureFormat format,
                const std::string &name = "");

  void update(void *data, uint32_t count);

  [[nodiscard]] WGPUBindGroupEntry createBindGroupEntry(uint32_t binding);

  [[nodiscard]] WGPUTextureView
  findOrCreateTextureView(WGPUTextureAspect aspect);

  void destroy();

private:
  WGPUTexture mTexture;
  std::unordered_map<uint32_t, WGPUTextureView> mViews;
};

class GraphicsProgramWebGPU {
public:
  [[nodiscard]] Result create(ShaderHandle sh, const std::string &name = "");

  [[nodiscard]] Result bind(WGPURenderPassEncoder renderPassEncoder) const;

  [[nodiscard]] inline const ShaderHandle getShader() const {
    return mShaderHandle;
  };

  void destroy();

private:
  ShaderHandle mShaderHandle;

  WGPUPipelineLayout mPipelineLayout;
  WGPURenderPipeline mPipeline;
};

class ComputeProgramWebGPU {
public:
  [[nodiscard]] Result create(const ShaderWebGPU *shader,
                              const std::string &name = "");

  [[nodiscard]] Result bind(WGPUComputePassEncoder renderPassEncoder) const;

  [[nodiscard]] inline const ShaderWebGPU *getShader() const {
    return mShader;
  };

  void destroy();

private:
  const ShaderWebGPU *mShader;

  WGPUPipelineLayout mPipelineLayout;
  WGPUComputePipeline mPipeline;
};

} // namespace cbz

#endif
