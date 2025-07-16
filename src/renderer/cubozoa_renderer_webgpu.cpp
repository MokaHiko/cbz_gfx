#include "cubozoa_renderer_webgpu.h"

#include "GLFW/glfw3.h"

#include "core/cubozoa_file.h"
#include "cubozoa/cubozoa_defines.h"
#include "cubozoa/net/cubozoa_net_http.h"
#include "cubozoa_irenderer_context.h"

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <webgpu/webgpu.h>

#include <murmurhash/MurmurHash3.h>
#include <nlohmann/json.hpp>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_wgpu.h>
#include <imgui.h>

#define CBZ_USE_SPRIV 1
#include <fstream>

constexpr static WGPUTextureDimension
TextureDimToWGPU(cbz::TextureDimension dim) {
  switch (dim) {
  case cbz::TextureDimension::e1D:
    return WGPUTextureDimension_1D;
  case cbz::TextureDimension::e2D:
    return WGPUTextureDimension_2D;
  case cbz::TextureDimension::e3D:
    return WGPUTextureDimension_3D;
  default:
    spdlog::error("Uknown WGPU format!");
    return {};
  }
}

static uint32_t IndexFormatGetSize(WGPUIndexFormat format) {
  switch (format) {
  case WGPUIndexFormat_Uint16:
    return 2;

  case WGPUIndexFormat_Uint32:
    return 4;

  case WGPUIndexFormat_Force32:
  case WGPUIndexFormat_Undefined:
    return 0;
    break;
  }

  return 0;
}

static std::shared_ptr<spdlog::logger> sLogger;
static std::unique_ptr<cbz::net::IHttpClient> sShaderHttpClient;

static WGPUDevice sDevice;
static WGPULimits sLimits;

static WGPUQueue sQueue;
static WGPUSurface sSurface;
static WGPUTextureFormat sSurfaceFormat;

static std::vector<cbz::VertexBufferWebGPU> sVertexBuffers;
static std::vector<cbz::IndexBufferWebGPU> sIndexBuffers;
static std::vector<cbz::UniformBufferWebWGPU> sUniformBuffers;
static std::vector<cbz::StorageBufferWebWGPU> sStorageBuffers;

static std::vector<cbz::TextureWebGPU> sTextures;
static std::unordered_map<uint32_t, WGPUSampler> sSamplers;

static std::vector<cbz::ShaderWebGPU> sShaders;
static std::unordered_map<uint32_t, WGPUBindGroup> sBindingGroups;

static std::vector<cbz::GraphicsProgramWebGPU> sGraphicsPrograms;
static std::vector<cbz::ComputeProgramWebGPU> sComputePrograms;

static std::pair<cbz::Result, WGPURequiredLimits>
CheckAndCreateRequiredLimits(WGPUAdapter adapter) {
  WGPUSupportedLimits supportedLimits = {};
  wgpuAdapterGetLimits(adapter, &supportedLimits);

  WGPURequiredLimits requiredLimits = {};
  requiredLimits.nextInChain = nullptr;
  requiredLimits.limits = supportedLimits.limits;

  sLogger->trace("Limits");
  sLogger->trace("- maxUniformBufferBindingSize : {}",
                 supportedLimits.limits.maxUniformBufferBindingSize);
  sLogger->trace("- maxBindGroups : {}", supportedLimits.limits.maxBindGroups);
  sLogger->trace("- maxBindingsPerBindGroup: {}",
                 supportedLimits.limits.maxBindingsPerBindGroup);

  requiredLimits.limits.maxVertexAttributes = 5;
  requiredLimits.limits.maxVertexBuffers = 1;
  requiredLimits.limits.maxBufferSize = supportedLimits.limits.maxBufferSize;
  requiredLimits.limits.maxVertexBufferArrayStride = sizeof(float) * 64;

#ifdef WEBGPU_BACKEND_EMSCRIPTEN
  requiredLimits.limits.maxInterStageShaderComponents = -1;
#endif

  // These two limits are different because they are "minimum" limits,
  // they are the only ones we may forward from the adapter's supported
  // limits.
  requiredLimits.limits.minUniformBufferOffsetAlignment =
      supportedLimits.limits.minUniformBufferOffsetAlignment;
  requiredLimits.limits.minStorageBufferOffsetAlignment =
      supportedLimits.limits.minStorageBufferOffsetAlignment;

  requiredLimits.limits.maxBindGroups = 2;
  requiredLimits.limits.maxUniformBuffersPerShaderStage =
      static_cast<uint32_t>(cbz::BufferSlot::eCount);
  requiredLimits.limits.maxUniformBufferBindingSize = 65536; // Default
  requiredLimits.limits.maxStorageBuffersPerShaderStage =
      static_cast<uint32_t>(cbz::BufferSlot::eCount);
  requiredLimits.limits.maxStorageBufferBindingSize =
      supportedLimits.limits.maxStorageBufferBindingSize;

  return {cbz::Result::eSuccess, requiredLimits};
};

static void DeviceLostCallback(WGPUDeviceLostReason reason, char const *message,
                               void *) {
  switch (reason) {
  case WGPUDeviceLostReason_Destroyed:
    sLogger->trace("{}", message);
    break;
  case WGPUDeviceLostReason_Undefined:
  case WGPUDeviceLostReason_Force32:
    sLogger->error("{}", message);
    break;
  }
}

static void UncapturedErrorCallback(WGPUErrorType type, char const *message,
                                    void *) {
  switch (type) {
  case WGPUErrorType_NoError:
    sLogger->trace("{}", message);
    break;
  case WGPUErrorType_Validation:
  case WGPUErrorType_OutOfMemory:
  case WGPUErrorType_Internal:
  case WGPUErrorType_Unknown:
  case WGPUErrorType_DeviceLost:
  case WGPUErrorType_Force32:
    sLogger->error("{}", message);
    std::abort();
    break;
  }
}

static void PollEvents([[maybe_unused]] bool yieldToBrowser) {
#ifdef WEBGPU_BACKEND_WGPU
  wgpuDevicePoll(sDevice, false, nullptr);
#else
#ifdef WEBGPU_BACKEND_EMSCRIPTEN
  if (yieldToBrowser) {
    emscripten_sleep(100);
  }
#endif
#endif
}

static void OnWorkDone(WGPUQueueWorkDoneStatus status, void *) {
  switch (status) {
  case WGPUQueueWorkDoneStatus_Success:
    sLogger->trace("Work complete!");
    break;
  case WGPUQueueWorkDoneStatus_Error:
  case WGPUQueueWorkDoneStatus_Unknown:
  case WGPUQueueWorkDoneStatus_DeviceLost:
  case WGPUQueueWorkDoneStatus_Force32:
    sLogger->error("Work failed {:#08x}!", static_cast<uint32_t>(status));
    break;
  }
}

namespace cbz {

class RendererContextWebGPU : public IRendererContext {
public:
  Result init(uint32_t width, uint32_t height, void *nwh) override;

  [[nodiscard]] Result vertexBufferCreate(VertexBufferHandle vbh,
                                          const VertexLayout &vertexLayout,
                                          uint32_t count,
                                          const void *data) override;

  void vertexBufferDestroy(VertexBufferHandle vbh) override;

  [[nodiscard]] Result indexBufferCreate(IndexBufferHandle ibh,
                                         IndexFormat format, uint32_t size,
                                         const void *data = nullptr) override;

  void indexBufferDestroy(IndexBufferHandle ibh) override;

  [[nodiscard]] Result uniformBufferCreate(UniformHandle uh, UniformType type,
                                           uint16_t num,
                                           const void *data = nullptr) override;

  void uniformBufferUpdate(UniformHandle uh, void *data, uint16_t num) override;

  void uniformBufferDestroy(UniformHandle uh) override;

  [[nodiscard]] Result
  structuredBufferCreate(StructuredBufferHandle sbh, UniformType type,
                         uint32_t num, const void *data = nullptr) override;

  void structuredBufferUpdate(StructuredBufferHandle sbh, uint32_t elementCount,
                              const void *data,
                              uint32_t elementOffset) override;

  void structuredBufferDestroy(StructuredBufferHandle sbh) override;

  [[nodiscard]] SamplerHandle
  getSampler(TextureBindingDesc texBindingDesc) override;

  [[nodiscard]] Result textureCreate(TextureHandle th, TextureFormat format,
                                     uint32_t x, uint32_t y, uint32_t z,
                                     TextureDimension dimension) override;

  void textureUpdate(TextureHandle th, void *data, uint32_t count) override;

  void textureDestroy(TextureHandle th) override;

  [[nodiscard]] Result shaderCreate(ShaderHandle sh,
                                    const std::string &path) override;

  void shaderDestroy(ShaderHandle sh) override;

  [[nodiscard]] Result graphicsProgramCreate(GraphicsProgramHandle gph,
                                             ShaderHandle sh) override;

  void graphicsProgramDestroy(GraphicsProgramHandle gph) override;

  [[nodiscard]] Result computeProgramCreate(ComputeProgramHandle cph,
                                            ShaderHandle sh) override;

  void computeProgramDestroy(ComputeProgramHandle cph) override;

  void shutdown() override;

  void submitSorted(const ShaderProgramCommand *sortedCmds,
                    uint32_t count) override;

private:
  [[nodiscard]] WGPUBindGroup findOrCreateBindGroup(ShaderHandle sh,
                                                    uint32_t descriptorHash,
                                                    const Binding *bindings,
                                                    uint32_t bindingCount);
};

Result VertexBufferWebGPU::create(const VertexLayout &vertexLayout,
                                  uint32_t count, const void *data,
                                  const std::string &name) {
  mVertexLayout = vertexLayout;

  uint32_t size = count *= mVertexLayout.stride;

  if (size <= 0) {
    sLogger->error("Cannot create vertex buffer with size 0!");
    return Result::eWGPUError;
  }

  if (size > sLimits.maxBufferSize) {
    sLogger->error("Cannot create vertex buffer with size > maxBufferSize({})!",
                   sLimits.maxBufferSize);
    return Result::eWGPUError;
  }

  mVertexCount = size / vertexLayout.stride;
  if (mVertexCount * vertexLayout.stride != size) {
    sLogger->warn("VertexBuffer size and layout do not match!");
  }

  WGPUBufferDescriptor bufferDesc = {};
  bufferDesc.nextInChain = nullptr;
  bufferDesc.label = name.c_str();
  bufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
  bufferDesc.size = (size + 3) & ~3; // Pad to multiple of 4
  bufferDesc.mappedAtCreation = false;

  mBuffer = wgpuDeviceCreateBuffer(sDevice, &bufferDesc);
  if (!mBuffer) {
    spdlog::error("Failed to create VertexBuffer!");
    return Result::eWGPUError;
  }

  if (data) {
    wgpuQueueWriteBuffer(sQueue, mBuffer, 0, data, size);
  }

  return Result::eSuccess;
};

Result VertexBufferWebGPU::bind(WGPURenderPassEncoder renderPassEncoder,
                                uint32_t slot) const {
  wgpuRenderPassEncoderSetVertexBuffer(renderPassEncoder, slot, mBuffer, 0,
                                       wgpuBufferGetSize(mBuffer));

  return Result::eSuccess;
}

void VertexBufferWebGPU::destroy() {
  if (!mBuffer) {
    spdlog::warn("Attempting to destroy invalid vertex buffer");
    return;
  }

  wgpuBufferDestroy(mBuffer);
}

Result IndexBufferWebGPU::create(WGPUIndexFormat format, uint32_t count,
                                 const void *data, const std::string &name) {
  const uint32_t size =
      IndexFormatGetSize(static_cast<WGPUIndexFormat>(format)) * count;

  if (size <= 0) {
    sLogger->error("Cannot create index buffer with size 0!");
    return Result::eWGPUError;
  }

  if (size > sLimits.maxBufferSize) {
    sLogger->error("Cannot create index buffer with size > maxBufferSize({})!",
                   sLimits.maxBufferSize);
    return Result::eWGPUError;
  }

  mIndexCount = size / IndexFormatGetSize(format);
  if (mIndexCount * IndexFormatGetSize(format) != size) {
    sLogger->warn("Index size and format do not match!");
  }
  mFormat = format;

  WGPUBufferDescriptor bufferDesc = {};
  bufferDesc.nextInChain = nullptr;
  bufferDesc.label = name.c_str();
  bufferDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
  bufferDesc.size = (size + 3) & ~3; // Pad to multiple of 4
  bufferDesc.mappedAtCreation = false;

  mBuffer = wgpuDeviceCreateBuffer(sDevice, &bufferDesc);
  if (!mBuffer) {
    spdlog::error("Failed to create IndexBuffer!");
    return Result::eWGPUError;
  }

  if (data) {
    wgpuQueueWriteBuffer(sQueue, mBuffer, 0, data, size);
  }

  return Result::eSuccess;
}

Result IndexBufferWebGPU::bind(WGPURenderPassEncoder renderPassEncoder) const {
  wgpuRenderPassEncoderSetIndexBuffer(renderPassEncoder, mBuffer, mFormat, 0,
                                      wgpuBufferGetSize(mBuffer));

  return Result::eSuccess;
}

void IndexBufferWebGPU::destroy() {
  if (!mBuffer) {
    spdlog::warn("Attempting to destroy invalid index buffer");
    return;
  }

  wgpuBufferDestroy(mBuffer);
}

[[nodiscard]] Result UniformBufferWebWGPU::create(UniformType type,
                                                  uint16_t num,
                                                  const void *data,
                                                  const std::string &name) {
  mElementType = type;
  mElementCount = num;
  uint32_t size = UniformTypeGetSize(mElementType) * mElementCount;

  if (size <= 0) {
    sLogger->error("Cannot create uniform '{}' buffer with size 0!", name);
    return Result::eWGPUError;
  }

  if (size > sLimits.maxUniformBufferBindingSize) {
    sLogger->error("Cannot create uniform buffer with size > "
                   "maxUniformBufferBindingSize({})!",
                   sLimits.maxBufferSize);
    return Result::eWGPUError;
  }

  WGPUBufferDescriptor bufferDesc = {};
  bufferDesc.nextInChain = nullptr;
  bufferDesc.label = name.c_str();
  bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
  bufferDesc.size = size;
  bufferDesc.mappedAtCreation = false;

  mBuffer = wgpuDeviceCreateBuffer(sDevice, &bufferDesc);
  if (!mBuffer) {
    spdlog::error("Failed to create Uniform buffer!");
    return Result::eWGPUError;
  }

  if (data) {
    wgpuQueueWriteBuffer(sQueue, mBuffer, 0, data, size);
  }

  return Result::eSuccess;
}

void UniformBufferWebWGPU::update(const void *data, uint16_t num) {
  uint32_t size = UniformTypeGetSize(mElementType) * num;

  if (num == 0) {
    size = UniformTypeGetSize(mElementType) * mElementCount;
  }

  wgpuQueueWriteBuffer(sQueue, mBuffer, 0, data, size);
}

void UniformBufferWebWGPU::destroy() {
  if (!mBuffer) {
    spdlog::warn("Attempting to destroy invalid uniform buffer");
    return;
  }

  wgpuBufferDestroy(mBuffer);
}

Result StorageBufferWebWGPU::create(UniformType type, uint32_t elementCount,
                                    const void *data, const std::string &name) {
  mElementType = type;
  mElementCount = elementCount;
  uint32_t size = UniformTypeGetSize(mElementType) * mElementCount;

  if (size <= 0) {
    sLogger->error("Cannot create uniform '{}' buffer with size 0!", name);
    return Result::eWGPUError;
  }

  if (size > sLimits.maxStorageBufferBindingSize) {
    sLogger->error("Cannot create uniform buffer with size > "
                   "maxStorageBufferBindingSize({})!",
                   sLimits.maxBufferSize);
    return Result::eWGPUError;
  }

  WGPUBufferDescriptor bufferDesc = {};
  bufferDesc.nextInChain = nullptr;
  bufferDesc.label = name.c_str();
  bufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst |
                     WGPUBufferUsage_CopySrc;
  bufferDesc.size = size;
  bufferDesc.mappedAtCreation = false;

  mBuffer = wgpuDeviceCreateBuffer(sDevice, &bufferDesc);
  if (!mBuffer) {
    spdlog::error("Failed to create Uniform buffer!");
    return Result::eWGPUError;
  }

  if (data) {
    wgpuQueueWriteBuffer(sQueue, mBuffer, 0, data, size);
  }

  return Result::eSuccess;
}

void StorageBufferWebWGPU::update(const void *data, uint32_t elementCount,
                                  uint32_t elementOffset) {
  uint32_t size = UniformTypeGetSize(mElementType) * elementCount;

  // 0 is considered whole size;
  if (elementCount == 0) {
    size = UniformTypeGetSize(mElementType) * mElementCount;
  }

  uint64_t offset = UniformTypeGetSize(mElementType) * elementOffset;

  if (size + offset > getSize()) {
    sLogger->error("Buffer update out of bounds: offset ({}) + size ({}) "
                   "exceeds buffer size ({}).",
                   offset, size, getSize());
    return;
  }

  wgpuQueueWriteBuffer(sQueue, mBuffer, offset, data, size);
}

void StorageBufferWebWGPU::destroy() {
  if (!mBuffer) {
    spdlog::warn("Attempting to destroy invalid storage buffer!");
    return;
  }

  wgpuBufferDestroy(mBuffer);
}

Result TextureWebGPU::create(uint32_t w, uint32_t h, uint32_t depth,
                             WGPUTextureDimension dimension,
                             WGPUTextureFormat format,
                             const std::string &name) {
  WGPUTextureDescriptor textDesc = {};
  textDesc.nextInChain = nullptr;
  textDesc.label = name.c_str();

  textDesc.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
  textDesc.dimension = dimension;
  textDesc.size.width = w;
  textDesc.size.height = h;
  textDesc.size.depthOrArrayLayers = depth;
  textDesc.format = format;
  textDesc.mipLevelCount = 1;
  textDesc.sampleCount = 1;
  textDesc.viewFormatCount = 0;
  textDesc.viewFormats = nullptr;

  mTexture = wgpuDeviceCreateTexture(sDevice, &textDesc);

  return Result::eSuccess;
}

void TextureWebGPU::update(void *data, uint32_t count) {
  const uint32_t formatSize = TextureFormatGetSize(
      static_cast<TextureFormat>(wgpuTextureGetFormat(mTexture)));
  const uint32_t size = formatSize * count;

  WGPUImageCopyTexture destination = {};
  destination.nextInChain = nullptr;
  destination.texture = mTexture;
  destination.mipLevel = 0;
  destination.origin = {0, 0, 0};
  destination.aspect = WGPUTextureAspect_All;

  WGPUTextureDataLayout dataLayout = {};
  dataLayout.nextInChain = nullptr;
  dataLayout.offset = 0;
  dataLayout.bytesPerRow = 4 * wgpuTextureGetWidth(mTexture);
  dataLayout.rowsPerImage = wgpuTextureGetHeight(mTexture);

  WGPUExtent3D extent = {};
  extent.width = wgpuTextureGetWidth(mTexture);
  extent.height = wgpuTextureGetHeight(mTexture);
  extent.depthOrArrayLayers = wgpuTextureGetDepthOrArrayLayers(mTexture);

  wgpuQueueWriteTexture(sQueue, &destination, data, size, &dataLayout, &extent);
}

WGPUBindGroupEntry TextureWebGPU::createBindGroupEntry(uint32_t binding) {
  WGPUBindGroupEntry entry = {};
  entry.nextInChain = nullptr;
  entry.binding = binding;
  entry.offset = 0;

  entry.textureView = findOrCreateTextureView(WGPUTextureAspect_All);

  return entry;
}

WGPUTextureView
TextureWebGPU::findOrCreateTextureView(WGPUTextureAspect aspect) {
  uint32_t textureViewKey;
  MurmurHash3_x86_32(&aspect, sizeof(WGPUTextureAspect), 0, &textureViewKey);

  if (mViews.find(textureViewKey) != mViews.end()) {
    return mViews[textureViewKey];
  }

  WGPUTextureViewDescriptor textureView = {};
  textureView.nextInChain = nullptr;

  textureView.format = wgpuTextureGetFormat(mTexture);
  switch (textureView.format) {
  case WGPUTextureFormat_RGBA8Unorm:
    textureView.aspect = WGPUTextureAspect_All;
    break;
  case WGPUTextureFormat_Undefined:
  default:
    sLogger->error("Unsupported aspect for texture!");
    break;
  };

  switch (wgpuTextureGetDimension(mTexture)) {
  case WGPUTextureDimension_2D:
    textureView.dimension = WGPUTextureViewDimension_2D;
    break;
  case WGPUTextureDimension_1D:
  case WGPUTextureDimension_3D:
  case WGPUTextureDimension_Force32:
  default:
    sLogger->error("Unsupported dimension for texture!");
    break;
  }
  textureView.baseMipLevel = 0;
  textureView.mipLevelCount = 1;
  textureView.baseArrayLayer = 0;
  textureView.arrayLayerCount = 1;
  textureView.aspect = aspect;

  return mViews[textureViewKey] = wgpuTextureCreateView(mTexture, &textureView);
}

void TextureWebGPU::destroy() {
  if (!mTexture) {
    sLogger->warn("Attempting to release uninitialized texture!");
    return;
  }

  for (auto it : mViews) {
    WGPUTextureView view = it.second;
    wgpuTextureViewRelease(view);
  }

  wgpuTextureRelease(mTexture);
}

void ShaderWebGPU::parseJsonRecursive(const nlohmann::json &varJson,
                                      bool isBinding, uint32_t offsets) {
  std::string name = varJson.value("name", "<unnamed>");

  bool isNewBinding = false;

  if (varJson.contains("binding")) {
    const auto &bindingJson = varJson["binding"];
    std::string bindingKind =
        bindingJson.value("kind", "<unknown_binding_kind");

    if (bindingKind == "descriptorTableSlot") {
      const auto &typeJson =
          varJson.contains("type") ? varJson["type"] : varJson;
      std::string typeKind = typeJson.value("kind", "<unknown_kind>");

      int bindingIndex = bindingJson.value("index", -1);
      if (typeKind != "struct") {
        uint32_t globalIndex = offsets + bindingIndex;
        if (globalIndex > std::numeric_limits<uint8_t>::max()) {
            sLogger->error("Binding index out of range {}", globalIndex);
        }
        mBindingDescs.push_back({});
        mBindingDescs.back().index = static_cast<uint8_t>(globalIndex);
        mBindingDescs.back().name = name;

        isNewBinding = true;

        sLogger->trace("binding(@{}): '{}'", mBindingDescs.back().index, name);
      } else {
        sLogger->trace("'{}' contains binding: ", name);
        offsets = bindingIndex;
      }
    }

    if (bindingKind == "uniform") {
      uint32_t offset = bindingJson.value("offset", -1);
      uint32_t size = bindingJson.value("size", -1);

      if (isBinding) {
        mBindingDescs.back().size =
            std::max(mBindingDescs.back().size, offset + size);
      }

      sLogger->trace("    - name: {}", name);
      sLogger->trace("    - offset: {}", offset);
      sLogger->trace("    - size: {}", size);
    }
  }

  const auto &typeJson = varJson.contains("type") ? varJson["type"] : varJson;
  std::string typeKind = typeJson.value("kind", "<unknown_kind>");

  sLogger->trace("    - kind: {}", typeKind);

  // Process kind
  if (typeKind == "scalar") {
    std::string scalarType =
        typeJson.value("scalarType", "<uknown_scalar_type>");

    if (isNewBinding) {
      mBindingDescs.back().type = BindingType::eUniformBuffer;
    }

    sLogger->trace("    - type: {}", scalarType);
    return;
  }

  if (typeKind == "vector") {
    uint32_t elementCount = typeJson.value("elementCount", 0);
    const auto &elementTypeJson = typeJson["elementType"];
    std::string scalarType =
        elementTypeJson.value("scalarType", "<uknown_scalar_type>");

    if (isNewBinding) {
      mBindingDescs.back().type = BindingType::eUniformBuffer;
    }

    sLogger->trace("    - type: {}x{}", scalarType, elementCount);
    return;
  }

  if (typeKind == "matrix") {
    uint32_t rowCount = typeJson.value("rowCount", 0);
    uint32_t colCount = typeJson.value("columnCount", 0);
    const auto &elementTypeJson = typeJson["elementType"];
    std::string scalarType =
        elementTypeJson.value("scalarType", "<uknown_scalar_type>");

    if (isNewBinding) {
      mBindingDescs.back().type = BindingType::eUniformBuffer;
      mBindingDescs.back().elementSize = rowCount * colCount * 4;
    }

    sLogger->trace("    - type: mat{}x{} ({})", rowCount, colCount, scalarType);
    return;
  }

  if (typeKind == "constantBuffer") {
    const auto &elementTypeJson = typeJson["elementType"];
    std::string elementKind = elementTypeJson.value("kind", "<unknown_kind>");

    sLogger->trace("    - elementKind: {}", elementKind);

    if (elementKind == "struct") {

      if (isNewBinding) {
        mBindingDescs.back().type = BindingType::eUniformBuffer;
      }

      for (const auto &fieldJson : elementTypeJson["fields"]) {
        parseJsonRecursive(fieldJson, isNewBinding, offsets);
      }
    }

    if (elementKind == "array") {

      const auto &arrayElementType = elementTypeJson["elementType"];
      for (const auto &field : arrayElementType["fields"]) {
        parseJsonRecursive(field, isNewBinding, offsets);
      }

      if (isNewBinding) {
        mBindingDescs.back().type = BindingType::eUniformBuffer;
        mBindingDescs.back().size *=
            (uint32_t)elementTypeJson.value("elementCount", 0);
      }
    }

    return;
  }

  if (typeKind == "samplerState") {
    if (isNewBinding) {
      mBindingDescs.back().type = BindingType::eSampler;
    }

    return;
  }

  if (typeKind == "struct") {
    for (const auto &field : typeJson["fields"]) {
      parseJsonRecursive(field, isBinding, offsets);
    }

    return;
  }

  if (typeKind == "resource") {
    std::string baseShape = typeJson.value("baseShape", "<unknown_base_shape>");

    if (isNewBinding) {
      if (baseShape == "structuredBuffer") {

        mBindingDescs.back().type = BindingType::eStructuredBuffer;
        if (typeJson.contains("access")) {
          if (typeJson["access"] == "readWrite") {
            mBindingDescs.back().type = BindingType::eRWStructuredBuffer;
          }
        }

        auto resultTypeJson = typeJson["resultType"];

        std::string resultTypeKind =
            resultTypeJson.value("kind", "<uknown_type_kind>");

        if (resultTypeKind == "vector") {
          parseJsonRecursive(resultTypeJson, true, offsets);
        } else if (resultTypeKind == "struct") {
          auto resultTypeFields = resultTypeJson["fields"];
          for (const auto &field : resultTypeFields) {
            parseJsonRecursive(field, true, offsets);
          }
        } else {
          sLogger->error("StructuredBuffer<{}> is not supported!",
                         resultTypeKind);
          return;
        };
      }

      if (baseShape == "texture2D") {
        mBindingDescs.back().type = BindingType::eTexture2D;
      }
    }

    sLogger->trace("    - resouceShape: {}", baseShape);
    return;
  }

  if (typeKind == "struct") {
    for (const auto &fieldJson : typeJson["fields"]) {
      parseJsonRecursive(fieldJson, false, offsets);
    }

    return;
  }

  sLogger->error("Cubozoa does not currently support '{}' for var {}!",
                 typeKind, name);
}

Result ShaderWebGPU::create(const std::string &path) {
  std::filesystem::path shaderPath = path;
  std::filesystem::path reflectionPath = path;
  reflectionPath.replace_extension(".json");

  std::ifstream reflectionStream(reflectionPath);
  nlohmann::json reflectionJson = nlohmann::json::parse(reflectionStream);

  // Parse uniforms
  for (const auto &paramJson : reflectionJson["parameters"]) {
    parseJsonRecursive(paramJson, false, 0);
  }

  // Vertex Input scope
  for (const auto &entryPoint : reflectionJson["entryPoints"]) {
    if (entryPoint.value("stage", "") == "fragment") {
      mStages |= WGPUShaderStage_Fragment;
    }

    if (entryPoint.value("stage", "") == "compute") {
      mStages |= WGPUShaderStage_Compute;
    }

    if (entryPoint.value("stage", "") == "vertex") {
      mStages |= WGPUShaderStage_Vertex;

      mVertexLayout.begin(VertexStepMode::eVertex);
      for (const auto &param : entryPoint["parameters"]) {
        auto fields = param["type"]["fields"];
        for (const auto &field : fields) {
          // Skipped built in
          std::string semanticName = field.value("semanticName", "");
          if (semanticName == "SV_INSTANCEID") {
            continue;
          }

          std::string name = field.value("name", "");
          auto binding = field["binding"];
          int location = binding.value("index", -1);
          int components = field["type"].value("elementCount", 1);
          std::string scalarType =
              field["type"]["elementType"].value("scalarType", "");

          sLogger->trace("Vertex Attribute: location={}, name={}, type={}x{}\n",
                         location, name, scalarType, components);

          VertexAttributeType attribute =
              static_cast<VertexAttributeType>(location);

          VertexFormat format = VertexFormat::eCount;

          if (scalarType == "float32") {
            format = static_cast<VertexFormat>(
                static_cast<uint32_t>(VertexFormat::eFloat32) + components - 1);
          }

          if (scalarType == "uint32") {
            format = static_cast<VertexFormat>(
                static_cast<uint32_t>(VertexFormat::eUint32) + components - 1);
          }
          if (scalarType == "sint32") {
            format = static_cast<VertexFormat>(
                static_cast<uint32_t>(VertexFormat::eSint32) + components - 1);
          }

          if (format == VertexFormat::eCount) {
            sLogger->error(
                "Failed to parse vertex entry attributes. Uknown format {}x{}",
                scalarType, components);
            return Result::eFailure;
          }

          mVertexLayout.push_attribute(attribute, format);
        }
      }
      mVertexLayout.end();
    }
  }

  std::vector<WGPUBindGroupLayoutEntry> bindingEntries(getBindings().size());

  for (size_t i = 0; i < bindingEntries.size(); i++) {
    bindingEntries[i] = {};
    bindingEntries[i].binding = mBindingDescs[i].index;
    bindingEntries[i].visibility = getShaderStages();

    switch (getBindings()[i].type) {
    case BindingType::eUniformBuffer:
      bindingEntries[i].buffer.type = WGPUBufferBindingType_Uniform;
      bindingEntries[i].buffer.nextInChain = nullptr;
      bindingEntries[i].buffer.hasDynamicOffset = false;
      bindingEntries[i].buffer.minBindingSize = getBindings()[i].size;
      break;

    case BindingType::eRWStructuredBuffer:
    case BindingType::eStructuredBuffer:
      bindingEntries[i].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
      if (mBindingDescs[i].type == BindingType::eRWStructuredBuffer) {
        bindingEntries[i].buffer.type = WGPUBufferBindingType_Storage;
      }

      bindingEntries[i].buffer.nextInChain = nullptr;
      bindingEntries[i].buffer.hasDynamicOffset = false;
      break;

    case BindingType::eSampler:
      bindingEntries[i].sampler.nextInChain = nullptr;
      bindingEntries[i].sampler.type = WGPUSamplerBindingType_Filtering;
      break;

    case BindingType::eTexture2D:
      bindingEntries[i].texture.nextInChain = nullptr;
      bindingEntries[i].texture.sampleType = WGPUTextureSampleType_Float;
      bindingEntries[i].texture.viewDimension = WGPUTextureViewDimension_2D;
      break;

    case BindingType::eNone:
      sLogger->error("Unsupported binding type <{}> for {}",
                     (uint32_t)getBindings()[i].type);
      break;
    }
  }

  WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = {};
  bindGroupLayoutDesc.nextInChain = nullptr;
  bindGroupLayoutDesc.label = "";
  bindGroupLayoutDesc.entryCount = static_cast<uint32_t>(bindingEntries.size());
  bindGroupLayoutDesc.entries = bindingEntries.data();

  mBindGroupLayout =
      wgpuDeviceCreateBindGroupLayout(sDevice, &bindGroupLayoutDesc);

  WGPUShaderModuleDescriptor shaderModuleDesc{};

#ifdef CBZ_USE_SPRIV
  std::vector<uint8_t> shaderSrcCode;
  if (LoadFileAsBinary(shaderPath.string(), shaderSrcCode) != Result::eSuccess) {
    return Result::eWGPUError;
  }

  WGPUShaderModuleSPIRVDescriptor shaderCodeDesc = {};
  shaderCodeDesc.chain.next = nullptr;
  shaderCodeDesc.chain.sType = WGPUSType_ShaderModuleSPIRVDescriptor;
  shaderCodeDesc.code = reinterpret_cast<const uint32_t*>(shaderSrcCode.data());
  shaderCodeDesc.codeSize = static_cast<uint32_t>(shaderSrcCode.size()) / sizeof(uint32_t);
#else
  std::string shaderSrcCode;
  if (LoadFileAsText(shaderPath.string(), shaderSrcCode) != Result::eSuccess) {
    return Result::eWGPUError;
  }

  std::string slangSrcCode;
  if (LoadFileAsText(shaderPath.string(), slangSrcCode) != Result::eSuccess) {
    return Result::eWGPUError;
  }
  WGPUShaderModuleWGSLDescriptor shaderCodeDesc = {};
  shaderCodeDesc.chain.next = nullptr;
  shaderCodeDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
  shaderCodeDesc.code = shaderSrcCode.c_str();
#endif

  shaderModuleDesc.nextInChain = &shaderCodeDesc.chain;
  shaderModuleDesc.label = path.c_str();
#ifdef WEBGPU_BACKEND_WGPU
  shaderModuleDesc.hintCount = 0;
  shaderModuleDesc.hints = nullptr;
#endif

  mModule = wgpuDeviceCreateShaderModule(sDevice, &shaderModuleDesc);
  if (!mModule) {
    return Result::eFailure;
  }

  return Result::eSuccess;
}

void ShaderWebGPU::destroy() { 
    wgpuShaderModuleRelease(mModule); 
    mModule = NULL;
}

Result GraphicsProgramWebGPU::create(ShaderHandle sh, const std::string &name) {
  mShaderHandle = sh;

  const ShaderWebGPU *shader = &sShaders[sh.idx];

  WGPURenderPipelineDescriptor pipelineDesc = {};
  pipelineDesc.nextInChain = nullptr;
  pipelineDesc.label = name.c_str();

  std::string layoutName = std::string(name) + std::string("_layout");
  WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
  pipelineLayoutDesc.nextInChain = nullptr;
  pipelineLayoutDesc.label = layoutName.c_str();
  pipelineLayoutDesc.bindGroupLayoutCount = 1;
  pipelineLayoutDesc.bindGroupLayouts = &shader->getBindGroupLayout();
  mPipelineLayout =
      wgpuDeviceCreatePipelineLayout(sDevice, &pipelineLayoutDesc);
  pipelineDesc.layout = mPipelineLayout;

  WGPUVertexBufferLayout vertexBufferLayout = {};
  vertexBufferLayout.arrayStride = shader->getVertexLayout().stride;
  vertexBufferLayout.stepMode =
      static_cast<WGPUVertexStepMode>(shader->getVertexLayout().stepMode);
  vertexBufferLayout.attributeCount =
      shader->getVertexLayout().attributes.size();
  vertexBufferLayout.attributes =
      (WGPUVertexAttribute const *)(shader->getVertexLayout()
                                        .attributes.data());

  WGPUVertexState vertexState = {};
  vertexState.nextInChain = nullptr;
  vertexState.module = shader->getModule();
  vertexState.entryPoint = "vertexMain";
  vertexState.constantCount = 0;
  vertexState.constants = nullptr;
  vertexState.bufferCount = 1;
  vertexState.buffers = &vertexBufferLayout;
  pipelineDesc.vertex = vertexState;

  WGPUPrimitiveState primitiveState = {};
  primitiveState.nextInChain = nullptr;
  primitiveState.topology = WGPUPrimitiveTopology_TriangleList;
  primitiveState.stripIndexFormat = WGPUIndexFormat_Undefined;
  primitiveState.frontFace = WGPUFrontFace_CW;
  // primitiveState.cullMode = WGPUCullMode_Back;
  primitiveState.cullMode = WGPUCullMode_None;
  pipelineDesc.primitive = primitiveState;

  pipelineDesc.depthStencil = nullptr;

  WGPUMultisampleState multiSampleState;
  multiSampleState.nextInChain = nullptr;
  multiSampleState.count = 1;
  multiSampleState.mask = ~0u;
  multiSampleState.alphaToCoverageEnabled = false;
  pipelineDesc.multisample = multiSampleState;

  // Calculation: rgb = srcFactor * srcRgb [operation] dstFactor * dstRgb
  WGPUBlendState blendState{};
  blendState.color = {
      WGPUBlendOperation_Add,
      WGPUBlendFactor_SrcAlpha,         // srcFactor
      WGPUBlendFactor_OneMinusSrcAlpha, // dstFactor
  };
  blendState.alpha = {
      WGPUBlendOperation_Add,
      WGPUBlendFactor_Zero, // srcFactor
      WGPUBlendFactor_One,  // dstFactor
  };

  WGPUColorTargetState targetState;
  targetState.nextInChain = nullptr;
  targetState.format = sSurfaceFormat;
  targetState.blend = &blendState;
  targetState.writeMask = WGPUColorWriteMask_All;

  WGPUFragmentState fragmentState = {};
  fragmentState.nextInChain = nullptr;
  if ((shader->getShaderStages() & WGPUShaderStage_Fragment) ==
      WGPUShaderStage_Fragment) {
    fragmentState.entryPoint = "fragmentMain";
    fragmentState.constantCount = 0;
    fragmentState.constants = nullptr;

    fragmentState.targetCount = 1;
    fragmentState.targets = &targetState;
    fragmentState.module = shader->getModule();
    pipelineDesc.fragment = &fragmentState;
  } else {
    pipelineDesc.fragment = nullptr;
  }

  mPipeline = wgpuDeviceCreateRenderPipeline(sDevice, &pipelineDesc);
  return Result::eSuccess;
}

Result
GraphicsProgramWebGPU::bind(WGPURenderPassEncoder renderPassEncoder) const {
  wgpuRenderPassEncoderSetPipeline(renderPassEncoder, mPipeline);
  return Result::eSuccess;
}

void GraphicsProgramWebGPU::destroy() {
  if (!mPipeline) {
    sLogger->warn("Attempting to destroy invalid graphics program!");
    return;
  }

  if (mPipelineLayout) {
    wgpuPipelineLayoutRelease(mPipelineLayout);
  }

  wgpuRenderPipelineRelease(mPipeline);
  mPipeline = NULL;
}

Result ComputeProgramWebGPU::create(ShaderHandle sh, const std::string &name) {
  mShaderHandle = sh;

  const ShaderWebGPU *shader = &sShaders[sh.idx];

  WGPUComputePipelineDescriptor pipelineDesc = {};
  pipelineDesc.nextInChain = nullptr;
  pipelineDesc.label = name.c_str();

  pipelineDesc.compute.module = shader->getModule();
  pipelineDesc.compute.entryPoint = "main";

  std::string layoutName = std::string(name) + std::string("_layout");
  WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
  pipelineLayoutDesc.nextInChain = nullptr;
  pipelineLayoutDesc.label = layoutName.c_str();
  pipelineLayoutDesc.bindGroupLayoutCount = 1;
  pipelineLayoutDesc.bindGroupLayouts = &shader->getBindGroupLayout();

  mPipelineLayout =
      wgpuDeviceCreatePipelineLayout(sDevice, &pipelineLayoutDesc);
  pipelineDesc.layout = mPipelineLayout;

  mPipeline = wgpuDeviceCreateComputePipeline(sDevice, &pipelineDesc);
  return Result::eSuccess;
}

Result
ComputeProgramWebGPU::bind(WGPUComputePassEncoder renderPassEncoder) const {
  wgpuComputePassEncoderSetPipeline(renderPassEncoder, mPipeline);
  return Result::eSuccess;
}

void ComputeProgramWebGPU::destroy() {
  if (!mPipeline) {
    sLogger->warn("Attempting to destroy invalid graphics program!");
    return;
  }

  if (mPipelineLayout) {
    wgpuPipelineLayoutRelease(mPipelineLayout);
  }

  wgpuComputePipelineRelease(mPipeline);
  mPipeline = NULL;
}

Result RendererContextWebGPU::init(uint32_t width, uint32_t height, void *nwh) {
  sLogger = spdlog::stdout_color_mt("wgpu");
  sLogger->set_level(spdlog::level::trace);
  sLogger->set_pattern("[%^%l%$][CBZ|RENDERER|WGPU] %v");

  //net::Endpoint cbzEndPoint = {
  //    net::Address("192.168.1.4"),
  //    net::Port(6000),
  //};

  //sShaderHttpClient = net::httpClientCreate(cbzEndPoint);
  //if (!sShaderHttpClient) {
  //  sLogger->error("Failed to connect to shader compilation sever!)");
  //  return Result::eFailure;
  //}

  //sLogger->info("Shader compiler backend connected ({}:{})", cbzEndPoint.address.c_str(), cbzEndPoint.port.c_str());

#ifdef WEBGPU_BACKEND_EMSCRIPTEN
  WGPUInstance instance = wgpuCreateInstance(nullptr);
#else
  WGPUInstanceDescriptor instanceDesc = {};
  WGPUInstance instance = wgpuCreateInstance(&instanceDesc);
#endif

  sSurface = glfwGetWGPUSurface(instance, static_cast<GLFWwindow *>(nwh));

  WGPURequestAdapterOptions adaptorOpts = {};
  adaptorOpts.nextInChain = nullptr;
  adaptorOpts.compatibleSurface = sSurface;
  adaptorOpts.powerPreference = WGPUPowerPreference_Undefined;
  adaptorOpts.backendType = WGPUBackendType_Undefined;
#ifdef WEBGPU_BACKEND_WGPU
  adaptorOpts.backendType = WGPUBackendType_Vulkan;
#endif
  adaptorOpts.forceFallbackAdapter = 0x00000000;

  struct AdapterRequest {
    WGPUAdapter adapter;
    bool finished;
    cbz::Result result;
  } adapterRequest;
  wgpuInstanceRequestAdapter(
      instance, &adaptorOpts,
      [](WGPURequestAdapterStatus status, WGPUAdapter adapter,
         char const *message, void *userdata) {
        AdapterRequest *request = static_cast<AdapterRequest *>(userdata);

        switch (status) {
        case WGPURequestAdapterStatus_Success:
          request->adapter = adapter;
          request->result = Result::eSuccess;
          break;
        case WGPURequestAdapterStatus_Unavailable:
        case WGPURequestAdapterStatus_Error:
        case WGPURequestAdapterStatus_Unknown:
        default:
          spdlog::error("{}", message);
          request->result = Result::eWGPUError;
          break;
        }

        request->finished = true;
      },
      &adapterRequest);
#ifdef __EMSCRIPTEN__
  while (!adapterRequest.finished) {
    emscripten_sleep(100);
  }
#endif
  if (adapterRequest.result != Result::eSuccess) {
    sLogger->error("Failed to request adapter!");
    return Result::eFailure;
  }

  WGPUAdapter adapter = adapterRequest.adapter;
  wgpuInstanceRelease(instance);

#ifndef __EMSCRIPTEN__
  WGPUSupportedLimits supportedLimits;
  supportedLimits.nextInChain = nullptr;

  wgpuAdapterGetLimits(adapter, &supportedLimits);
  spdlog::trace("Adapter limits:");
  spdlog::trace("- maxTextureDimension2D: {}",
                supportedLimits.limits.maxTextureDimension2D);
  spdlog::trace("- maxBufferSize: {}", supportedLimits.limits.maxBufferSize);

  size_t adapterFeatureCount = wgpuAdapterEnumerateFeatures(adapter, nullptr);
  std::vector<WGPUFeatureName> adapterFeatures(adapterFeatureCount);
  wgpuAdapterEnumerateFeatures(adapter, adapterFeatures.data());
  // for (WGPUFeatureName _ : adapterFeatures) { }

  WGPUAdapterProperties adapterProperties;
  wgpuAdapterGetProperties(adapter, &adapterProperties);
  sLogger->trace("- name: {}", adapterProperties.name);
  sLogger->trace("- vendorName: {}", adapterProperties.vendorName);
  sLogger->trace("- driverDesc: {}", adapterProperties.driverDescription);
  sLogger->trace("- adapterType: {:#08x}",
                 static_cast<uint32_t>(adapterProperties.adapterType));
  sLogger->trace("- architecture: {:#08x}",
                 static_cast<uint32_t>(adapterProperties.backendType));
#endif

  auto [requiredLimitsRes, requiredLimits] =
      CheckAndCreateRequiredLimits(adapter);
  if (requiredLimitsRes != Result::eSuccess) {
    return requiredLimitsRes;
  }
  sLimits = requiredLimits.limits;
  WGPUDeviceDescriptor deviceDesc = {};
  deviceDesc.nextInChain = nullptr;
  deviceDesc.label = "WGPUDevice";
  deviceDesc.requiredFeatures = nullptr;
  deviceDesc.requiredLimits = &requiredLimits;
  deviceDesc.defaultQueue.nextInChain = nullptr;
  deviceDesc.defaultQueue.label = "DefaultQueue";
  deviceDesc.deviceLostCallback = DeviceLostCallback;

  struct DeviceRequest {
    WGPUDevice device;
    bool finished;
    cbz::Result result;
  } deviceRequest;
  wgpuAdapterRequestDevice(
      adapter, &deviceDesc,
      [](WGPURequestDeviceStatus status, WGPUDevice device, char const *message,
         void *userdata) {
        DeviceRequest *request = static_cast<DeviceRequest *>(userdata);

        switch (status) {
        case WGPURequestDeviceStatus_Success:
          request->device = device;
          request->result = Result::eSuccess;
          break;
        case WGPURequestDeviceStatus_Error:
        case WGPURequestDeviceStatus_Unknown:
        case WGPURequestDeviceStatus_Force32:
          request->result = Result::eWGPUError;
          spdlog::error("{}", message);
          break;
        }

        request->finished = true;
      },
      &deviceRequest);
#ifdef __EMSCRIPTEN__
  while (!deviceRequest.finished) {
    emscripten_sleep(100);
  }
#endif
  if (deviceRequest.result != Result::eSuccess) {
    return Result::eWGPUError;
  }

  sDevice = deviceRequest.device;

  wgpuDeviceSetUncapturedErrorCallback(sDevice, UncapturedErrorCallback,
                                       nullptr);
  sQueue = wgpuDeviceGetQueue(sDevice);
  wgpuQueueOnSubmittedWorkDone(sQueue, OnWorkDone, nullptr);

  sSurfaceFormat = WGPUTextureFormat_BGRA8Unorm;
  wgpuAdapterRelease(adapter);

  WGPUSurfaceConfiguration surfaceConfig = {};
  surfaceConfig.nextInChain = nullptr;
  surfaceConfig.device = sDevice;
  surfaceConfig.format = sSurfaceFormat;
  surfaceConfig.usage =
      WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopyDst;
  surfaceConfig.viewFormatCount = 0;
  surfaceConfig.viewFormats = nullptr;
  surfaceConfig.alphaMode = WGPUCompositeAlphaMode_Auto;
  surfaceConfig.width = width;
  surfaceConfig.height = height;
  surfaceConfig.presentMode = WGPUPresentMode_Fifo;
  wgpuSurfaceConfigure(sSurface, &surfaceConfig);

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO();

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOther(static_cast<GLFWwindow *>(nwh), true);
  ImGui_ImplWGPU_Init(sDevice, 3, sSurfaceFormat);

  sLogger->info("Cubozoa initialized!");
  return Result::eSuccess;
}

uint32_t frameCounter = 0;
void RendererContextWebGPU::submitSorted(const ShaderProgramCommand *sortedCmds,
                                         uint32_t count) {
  WGPUSurfaceTexture surfaceTexture;
  wgpuSurfaceGetCurrentTexture(sSurface, &surfaceTexture);
  switch (surfaceTexture.status) {

  case WGPUSurfaceGetCurrentTextureStatus_Success:
    break;
  case WGPUSurfaceGetCurrentTextureStatus_Timeout:
  case WGPUSurfaceGetCurrentTextureStatus_Outdated:
  case WGPUSurfaceGetCurrentTextureStatus_Lost:
  case WGPUSurfaceGetCurrentTextureStatus_OutOfMemory:
  case WGPUSurfaceGetCurrentTextureStatus_DeviceLost:
  case WGPUSurfaceGetCurrentTextureStatus_Force32:
    sLogger->error("Failed to get surface texture!");
    return;
  }

  WGPUTextureViewDescriptor surfaceTextureViewDesc{};
  surfaceTextureViewDesc.nextInChain = NULL;
  surfaceTextureViewDesc.label = "surfaceTextureViewX";
  surfaceTextureViewDesc.format = wgpuTextureGetFormat(surfaceTexture.texture);
  surfaceTextureViewDesc.dimension = WGPUTextureViewDimension_2D;
  surfaceTextureViewDesc.baseMipLevel = 0;
  surfaceTextureViewDesc.mipLevelCount = 1;
  surfaceTextureViewDesc.baseArrayLayer = 0;
  surfaceTextureViewDesc.arrayLayerCount = 1;
  surfaceTextureViewDesc.aspect = WGPUTextureAspect_All;
  WGPUTextureView surfaceTextureView =
      wgpuTextureCreateView(surfaceTexture.texture, &surfaceTextureViewDesc);

  WGPUCommandEncoderDescriptor cmdEncoderDesc = {};
  cmdEncoderDesc.nextInChain = nullptr;
  cmdEncoderDesc.label = "CommandEncoderFrameX";

  WGPUCommandEncoder cmdEncoder =
      wgpuDeviceCreateCommandEncoder(sDevice, &cmdEncoderDesc);

  // Target struct
  uint8_t target = std::numeric_limits<uint8_t>::max();
  TargetType targetType = TargetType::eNone;
  // Attachments if graphics
  // WGPURenderPassEncoder renderPassEncoder = nullptr;
  // WGPUComputePassEncoder computePassEncoder = nullptr;

  uint64_t targetSortKey = std::numeric_limits<uint64_t>::max();

  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;
  bool isIndexed = false;
  WGPURenderPassEncoder renderPassEncoder = nullptr;

  uint32_t dispatchX = 0;
  uint32_t dispatchY = 0;
  uint32_t dispatchZ = 0;
  WGPUComputePassEncoder computePassEncoder = nullptr;

  for (uint32_t cmdIdx = 0; cmdIdx < count; cmdIdx++) {
    const ShaderProgramCommand &renderCmd = sortedCmds[cmdIdx];

    // TODO:
    // End if different target
    // Begin target

    // Begin target encoder
    if (target != renderCmd.target) {
      target = renderCmd.target;
      targetType = renderCmd.programType;

      switch (renderCmd.programType) {

      case TargetType::eCompute: {
        WGPUComputePassDescriptor computePassDesc = {};
        computePassDesc.nextInChain = nullptr;
        computePassDesc.label = "computePassX";
        computePassDesc.timestampWrites = nullptr;

        computePassEncoder =
            wgpuCommandEncoderBeginComputePass(cmdEncoder, &computePassDesc);
      } break;

      case TargetType::eGraphics: {
        WGPURenderPassColorAttachment renderPassColorAttachmentDesc = {};
        renderPassColorAttachmentDesc.nextInChain = nullptr;
        renderPassColorAttachmentDesc.view = surfaceTextureView;
        // renderPassColorAttachmentDesc.resolveTarget ;
        renderPassColorAttachmentDesc.loadOp = WGPULoadOp_Clear;
        renderPassColorAttachmentDesc.storeOp = WGPUStoreOp_Store;
        renderPassColorAttachmentDesc.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};

#ifndef WEBGPU_BACKEND_WGPU
        renderPassColorAttachmentDesc.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif // NOT WEBGPU_BACKEND_WGPU

        WGPURenderPassDescriptor renderPassDesc = {};
        renderPassDesc.nextInChain = nullptr;
        renderPassDesc.label = "RenderPassDescX";
        renderPassDesc.colorAttachmentCount = 1;
        renderPassDesc.colorAttachments = &renderPassColorAttachmentDesc;
        renderPassDesc.depthStencilAttachment = nullptr;
        renderPassDesc.occlusionQuerySet = nullptr;
        renderPassDesc.timestampWrites = nullptr;

        renderPassEncoder =
            wgpuCommandEncoderBeginRenderPass(cmdEncoder, &renderPassDesc);
      }

      case TargetType::eNone: {
      } break;
      }
    }

    // Execute cmds
    switch (targetType) {

    case TargetType::eCompute: {
      if (targetSortKey != renderCmd.sortKey) {
        targetSortKey = renderCmd.sortKey;

        const ComputeProgramWebGPU &computeProgram =
            sComputePrograms[renderCmd.program.compute.ph.idx];

        if (computeProgram.bind(computePassEncoder) != Result::eSuccess) {
          continue;
        }

        const WGPUBindGroup computeBindGroup = findOrCreateBindGroup(
            computeProgram.getShader(), renderCmd.getDescriptorHash(),
            renderCmd.bindings.data(),
            static_cast<uint32_t>(renderCmd.bindings.size()));

        if (computeBindGroup) {
          uint32_t offsets = 0;
          wgpuComputePassEncoderSetBindGroup(computePassEncoder, 0,
                                             computeBindGroup, 0, &offsets);
        }

        dispatchX = renderCmd.program.compute.x;
        dispatchY = renderCmd.program.compute.y;
        dispatchZ = renderCmd.program.compute.z;
      }

      wgpuComputePassEncoderDispatchWorkgroups(computePassEncoder, dispatchX,
                                               dispatchY, dispatchZ);

      wgpuComputePassEncoderEnd(computePassEncoder);
      wgpuComputePassEncoderRelease(computePassEncoder);
      computePassEncoder = nullptr;
    } break;

    case TargetType::eGraphics: {
      if (targetSortKey != renderCmd.sortKey) {
        targetSortKey = renderCmd.sortKey;

        const GraphicsProgramWebGPU &graphicsProgram =
            sGraphicsPrograms[renderCmd.program.graphics.ph.idx];

        const VertexBufferWebGPU &vb =
            sVertexBuffers[renderCmd.program.graphics.vbh.idx];

        if (sShaders[graphicsProgram.getShader().idx].getVertexLayout() !=
            vb.getVertexLayout()) {
          sLogger->warn("Incompatible vertex buffer and program layout for {}",
                        HandleProvider<GraphicsProgramHandle>::getName(
                            renderCmd.program.graphics.ph));
          sLogger->warn("Discarding draw...");
          continue;
        }

        if (graphicsProgram.bind(renderPassEncoder) != Result::eSuccess) {
          continue;
        }

        const WGPUBindGroup graphicsBindGroup = findOrCreateBindGroup(
            graphicsProgram.getShader(), renderCmd.getDescriptorHash(),
            renderCmd.bindings.data(),
            static_cast<uint32_t>(renderCmd.bindings.size()));

        if (graphicsBindGroup) {
          uint32_t offsets = 0;
          wgpuRenderPassEncoderSetBindGroup(renderPassEncoder, 0,
                                            graphicsBindGroup, 0, &offsets);
        }

        if (vb.bind(renderPassEncoder) != Result::eSuccess) {
          continue;
        };

        vertexCount = vb.getVertexCount();

        if (renderCmd.program.graphics.ibh.idx != CBZ_INVALID_HANDLE) {
          const IndexBufferWebGPU &ib =
              sIndexBuffers[renderCmd.program.graphics.ibh.idx];

          if (ib.bind(renderPassEncoder) != Result::eSuccess) {
            continue;
          }

          indexCount = ib.getIndexCount();
          isIndexed = true;
        } else {
          indexCount = 0;
          isIndexed = false;
        }

        if (isIndexed) {
          wgpuRenderPassEncoderDrawIndexed(renderPassEncoder, indexCount, 1, 0,
                                           0, 0);
        } else {
          wgpuRenderPassEncoderDraw(renderPassEncoder, vertexCount, 1, 0, 0);
        }

        // TODO: Move to swapchain render target
        ImGui_ImplWGPU_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Profiling");
        ImGui::Text("fps: %3.0f", ImGui::GetIO().Framerate);

        static float vec[3];
        ImGui::SliderFloat3("Color", vec, 0.0f, 100.0f);

        ImGui::End();

        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), renderPassEncoder);

        wgpuRenderPassEncoderEnd(renderPassEncoder);
        wgpuRenderPassEncoderRelease(renderPassEncoder);
        renderPassEncoder = nullptr;
      };

    } break;

    case TargetType::eNone: {
      sLogger->critical("Uknown render target!");
    } break;
    }
  }

  WGPUCommandBufferDescriptor cmdDesc = {};
  cmdDesc.nextInChain = nullptr;
  cmdDesc.label = "CommandBufferX";

  WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(cmdEncoder, &cmdDesc);
  wgpuCommandEncoderRelease(cmdEncoder);

  wgpuQueueSubmit(sQueue, 1, &cmd);
  wgpuCommandBufferRelease(cmd);

#ifndef __EMSCRIPTEN__
  wgpuSurfacePresent(sSurface);
#endif

  wgpuTextureRelease(surfaceTexture.texture);
  wgpuTextureViewRelease(surfaceTextureView);

  PollEvents(false);
  frameCounter++;
}

Result
RendererContextWebGPU::vertexBufferCreate(VertexBufferHandle vbh,
                                          const VertexLayout &vertexLayout,
                                          uint32_t count, const void *data) {
  if (sVertexBuffers.size() < vbh.idx + 1) {
    sVertexBuffers.resize(vbh.idx + 1);
  }

  return sVertexBuffers[vbh.idx].create(vertexLayout, count, data);
}

void RendererContextWebGPU::vertexBufferDestroy(VertexBufferHandle vbh) {
  return sVertexBuffers[vbh.idx].destroy();
}

Result RendererContextWebGPU::indexBufferCreate(IndexBufferHandle ibh,
                                                IndexFormat format,
                                                uint32_t count,
                                                const void *data) {
  if (sIndexBuffers.size() < ibh.idx + 1) {
    sIndexBuffers.resize(ibh.idx + 1);
  }

  return sIndexBuffers[ibh.idx].create(static_cast<WGPUIndexFormat>(format),
                                       count, data);
}

void RendererContextWebGPU::indexBufferDestroy(IndexBufferHandle ibh) {
  return sIndexBuffers[ibh.idx].destroy();
}

Result RendererContextWebGPU::uniformBufferCreate(UniformHandle uh,
                                                  UniformType type,
                                                  uint16_t num,
                                                  const void *data) {
  if (sUniformBuffers.size() < uh.idx + 1) {
    sUniformBuffers.resize(uh.idx + 1);
  }

  return sUniformBuffers[uh.idx].create(
      type, num, data, HandleProvider<UniformHandle>::getName(uh));
}

void RendererContextWebGPU::uniformBufferUpdate(UniformHandle uh, void *data,
                                                uint16_t num) {
  sUniformBuffers[uh.idx].update(data, num);
}

void RendererContextWebGPU::uniformBufferDestroy(UniformHandle uh) {
  return sUniformBuffers[uh.idx].destroy();
}

Result RendererContextWebGPU::structuredBufferCreate(StructuredBufferHandle sbh,
                                                     UniformType type,
                                                     uint32_t num,
                                                     const void *data) {
  if (sStorageBuffers.size() < sbh.idx + 1) {
    sStorageBuffers.resize(sbh.idx + 1);
  }

  return sStorageBuffers[sbh.idx].create(
      type, num, data, HandleProvider<StructuredBufferHandle>::getName(sbh));
};

void RendererContextWebGPU::structuredBufferUpdate(StructuredBufferHandle sbh,
                                                   uint32_t elementCount,
                                                   const void *data,
                                                   uint32_t elementOffset) {
  sStorageBuffers[sbh.idx].update(data, elementCount, elementOffset);
}

void RendererContextWebGPU::structuredBufferDestroy(
    StructuredBufferHandle sbh) {
  return sUniformBuffers[sbh.idx].destroy();
}

SamplerHandle
RendererContextWebGPU::getSampler(TextureBindingDesc texBindingDesc) {
  uint32_t samplerID;
  MurmurHash3_x86_32(&texBindingDesc, sizeof(texBindingDesc), 0, &samplerID);

  if (sSamplers.find(samplerID) != sSamplers.end()) {
    return SamplerHandle{samplerID};
  }

  WGPUSamplerDescriptor samplerDesc = {};
  samplerDesc.nextInChain = nullptr;
  samplerDesc.addressModeU =
      static_cast<WGPUAddressMode>(texBindingDesc.addressMode);
  samplerDesc.addressModeV =
      static_cast<WGPUAddressMode>(texBindingDesc.addressMode);
  samplerDesc.addressModeW =
      static_cast<WGPUAddressMode>(texBindingDesc.addressMode);
  samplerDesc.magFilter =
      static_cast<WGPUFilterMode>(texBindingDesc.filterMode);
  samplerDesc.minFilter =
      static_cast<WGPUFilterMode>(texBindingDesc.filterMode);

  // samplerDesc.mipmapFilter;
  // samplerDesc.lodMinClamp;
  // samplerDesc.lodMaxClamp;
  // samplerDesc.compare;
  samplerDesc.maxAnisotropy = 1;

  WGPUSampler sampler = wgpuDeviceCreateSampler(sDevice, &samplerDesc);
  sSamplers[samplerID] = sampler;
  return SamplerHandle{samplerID};
};

Result RendererContextWebGPU::textureCreate(TextureHandle th,
                                            TextureFormat format, uint32_t w,
                                            uint32_t h, uint32_t depth,
                                            TextureDimension dimension) {
  if (sTextures.size() < th.idx + 1) {
    sTextures.resize(th.idx + 1);
  }

  return sTextures[th.idx].create(w, h, depth, TextureDimToWGPU(dimension),
                                  static_cast<WGPUTextureFormat>(format));
}

void RendererContextWebGPU::textureUpdate(TextureHandle th, void *data,
                                          uint32_t count) {
  sTextures[th.idx].update(data, count);
};

void RendererContextWebGPU::textureDestroy(TextureHandle th) {
  return sTextures[th.idx].destroy();
};

Result RendererContextWebGPU::shaderCreate(ShaderHandle sh,
                                           const std::string &path) {
  if (sShaders.size() < sh.idx + 1) {
    sShaders.resize(sh.idx + 1);
  }

  return sShaders[sh.idx].create(path);
}

void RendererContextWebGPU::shaderDestroy(ShaderHandle sh) {
  return sShaders[sh.idx].destroy();
}

Result RendererContextWebGPU::graphicsProgramCreate(GraphicsProgramHandle gph,
                                                    ShaderHandle sh) {
  if (sGraphicsPrograms.size() < gph.idx + 1) {
    sGraphicsPrograms.resize(gph.idx + 1);
  }

  return sGraphicsPrograms[gph.idx].create(
      sh, HandleProvider<GraphicsProgramHandle>::getName(gph));
}

void RendererContextWebGPU::graphicsProgramDestroy(GraphicsProgramHandle gph) {
  return sGraphicsPrograms[gph.idx].destroy();
}

Result RendererContextWebGPU::computeProgramCreate(ComputeProgramHandle cph,
                                                   ShaderHandle sh) {
  if (sComputePrograms.size() < cph.idx + 1) {
    sComputePrograms.resize(cph.idx + 1);
  }

  return sComputePrograms[cph.idx].create(sh);
}

void RendererContextWebGPU::computeProgramDestroy(ComputeProgramHandle cph) {
  return sComputePrograms[cph.idx].destroy();
}

void RendererContextWebGPU::shutdown() {
  ImGui_ImplGlfw_Shutdown();
  ImGui_ImplWGPU_Shutdown();

  wgpuSurfaceRelease(sSurface);
  wgpuDeviceRelease(sDevice);
}

WGPUBindGroup RendererContextWebGPU::findOrCreateBindGroup(
    ShaderHandle sh, uint32_t descriptorHash, const Binding *bindings,
    uint32_t bindingCount) {
  if (!bindingCount || !bindings) {
    return nullptr;
  }

  const ShaderWebGPU *shader = &sShaders[sh.idx];
  const std::vector<BindingDesc> &shaderBindingDescs = shader->getBindings();

  if (sBindingGroups.find(descriptorHash) == sBindingGroups.end()) {

    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.nextInChain = nullptr;
    bindGroupDesc.label = nullptr;
    bindGroupDesc.layout = shader->getBindGroupLayout();

    std::vector<WGPUBindGroupEntry> bindGroupEntries(bindingCount);

    for (uint32_t i = 0; i < static_cast<uint32_t>(shaderBindingDescs.size());
         i++) {
      switch (bindings[i].type) {
      case BindingType::eUniformBuffer: {
        switch (bindings[i].value.uniformBuffer.valueType) {
        case UniformType::eVec4:
        case UniformType::eMat4: {
          UniformHandle uh = bindings[i].value.uniformBuffer.handle;

          const auto &it =
              std::find_if(shaderBindingDescs.begin(), shaderBindingDescs.end(),
                           [=](const BindingDesc &bindingDesc) {
                             return bindingDesc.name ==
                                    HandleProvider<UniformHandle>::getName(uh);
                           });

          if (it == shaderBindingDescs.end()) {
            sLogger->error(
                "Shader program '{}' has no uniform binding named '{}'",
                HandleProvider<ShaderHandle>::getName(sh),
                HandleProvider<UniformHandle>::getName(uh));
            return nullptr;
          }

          bindGroupEntries[i] =
              sUniformBuffers[uh.idx].createBindGroupEntry(it->index);
        } break;

        default:
          sLogger->error("Unsupported uniform buffer ShaderValueType!");
          break;
        }
      } break;

      case BindingType::eRWStructuredBuffer:
      case BindingType::eStructuredBuffer: {
        switch (bindings[i].value.storageBuffer.valueType) {
        case UniformType::eVec4:
        case UniformType::eMat4: {
          StructuredBufferHandle sbh = bindings[i].value.storageBuffer.handle;

          const auto &it =
              std::find_if(shaderBindingDescs.begin(), shaderBindingDescs.end(),
                           [&](const BindingDesc &bindingDesc) {
                             return bindingDesc.index ==
                                    bindings[i].value.storageBuffer.slot;
                           });

          if (it == shaderBindingDescs.end()) {
            sLogger->error(
                "Bound program has no storage binding named {}",
                HandleProvider<StructuredBufferHandle>::getName(sbh));
            return nullptr;
          }

          bindGroupEntries[i] =
              sStorageBuffers[sbh.idx].createBindGroupEntry(it->index);
        } break;

        default:
          sLogger->error("Unsupported storage buffer ShaderValueType!");
          break;
        }
      } break;

      case BindingType::eTexture2D: {
        TextureHandle th = bindings[i].value.texture.handle;

        const auto &it = std::find_if(
            shaderBindingDescs.begin(), shaderBindingDescs.end(),
            [=](const BindingDesc &bindingDesc) {
              return bindingDesc.index == bindings[i].value.texture.slot;
            });

        if (it->type != BindingType::eTexture2D) {
          sLogger->error(
              "Bound program has uniform binding and type mismatch for '{}'",
              HandleProvider<TextureHandle>::getName(th));
          return nullptr;
        }

        if (it == shaderBindingDescs.end()) {
          sLogger->error(
              "Shader program '{}' has no uniform binding named '{}'",
              HandleProvider<ShaderHandle>::getName(sh),
              HandleProvider<TextureHandle>::getName(th));
          return nullptr;
        }

        bindGroupEntries[i] = sTextures[th.idx].createBindGroupEntry(it->index);
      } break;

      case BindingType::eSampler: {
        SamplerHandle smplerHandle = bindings[i].value.sampler.handle;

        const auto &it = std::find_if(
            shaderBindingDescs.begin(), shaderBindingDescs.end(),
            [=](const BindingDesc &bindingDesc) {
              return bindingDesc.index == bindings[i].value.sampler.slot;
            });

        if (it->type != BindingType::eSampler) {
          sLogger->error("Shader program '{}' has type mismatch",
                         HandleProvider<ShaderHandle>::getName(sh));
          return nullptr;
        }

        if (it == shaderBindingDescs.end()) {
          sLogger->error(
              "Shader program '{}' has no uniform binding of type <Sampler>",
              HandleProvider<ShaderHandle>::getName(sh));
          return nullptr;
        }

        WGPUBindGroupEntry entry = {};
        entry.binding = it->index;
        entry.nextInChain = nullptr;
        entry.sampler = sSamplers[smplerHandle.idx];
        bindGroupEntries[i] = entry;
      } break;

      case BindingType::eNone:
        sLogger->error("Uknown and unsupported binding!");
        break;
      }
    }

    bindGroupDesc.entryCount = bindingCount;
    bindGroupDesc.entries = bindGroupEntries.data();

    return sBindingGroups[descriptorHash] =
               wgpuDeviceCreateBindGroup(sDevice, &bindGroupDesc);
  }

  return sBindingGroups[descriptorHash];
}

} // namespace cbz

std::unique_ptr<cbz::IRendererContext> RendererContextCreate() {
  return std::make_unique<cbz::RendererContextWebGPU>();
}
