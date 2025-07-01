#include "cubozoa_renderer_webgpu.h"

#include "core/cubozoa_file.h"
#include "cubozoa_irenderer_context.h"

#include <webgpu/webgpu.h>

#include <murmurhash/MurmurHash3.h>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_wgpu.h>
#include <imgui.h>

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
}

static std::shared_ptr<spdlog::logger> sLogger;

static WGPUDevice sDevice;
static WGPULimits sLimits;

static WGPUQueue sQueue;
static WGPUSurface sSurface;
static WGPUTextureFormat sSurfaceFormat;

static std::pair<cbz::Result, WGPURequiredLimits>
CheckAndCreateRequiredLimits(WGPUAdapter adapter) {
  WGPUSupportedLimits supportedLimits;
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
  requiredLimits.limits.maxUniformBuffersPerShaderStage = 1;
  requiredLimits.limits.maxUniformBufferBindingSize = 65536; // Minimum

  requiredLimits.limits.maxStorageBuffersPerShaderStage = 1;
  requiredLimits.limits.maxStorageBufferBindingSize = 1;

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

  void uniformBufferUpdate(UniformHandle uh, void *data, uint32_t num) override;

  void uniformBufferDestroy(UniformHandle uh) override;

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

  void shutdown() override;

  void drawSorted(const std::vector<RenderCommand> &sortedDraws) override;

private:
  std::vector<VertexBufferWebGPU> mVertexBuffers;
  std::vector<IndexBufferWebGPU> mIndexBuffers;
  std::vector<UniformBufferWebWGPU> mUniformBuffers;

  std::vector<TextureWebGPU> mTextures;

  std::unordered_map<uint32_t, WGPUBindGroup> mBindingGroups;
  std::unordered_map<uint32_t, WGPUSampler> mSamplers;

  std::vector<ShaderWebGPU> mShaders;
  std::vector<GraphicsProgramWebGPU> mGraphicsPrograms;
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
    // TODO: Handle queue write buffer allignment.
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
    // TODO: Handle queue write buffer allignment.
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
    // TODO: Handle queue write buffer allignment.
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

  uint32_t size = TextureFormatGetSize(static_cast<TextureFormat>(
                      wgpuTextureGetFormat(mTexture))) *
                  count;

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
                                      bool isBinding) {
  std::string name = varJson.value("name", "<unnamed>");

  const auto &bindingJson = varJson["binding"];
  std::string bindingKind = bindingJson.value("kind", "<unknown_binding_kind");

  bool isNewBinding = false;
  if (bindingKind == "descriptorTableSlot") {
    int bindingIndex = bindingJson.value("index", -1);

    mBindings.push_back({});

    mBindings[bindingIndex].name = name;
    isNewBinding = true;

    sLogger->trace("binding(@{}): {}", bindingIndex, name);
  }

  if (bindingKind == "uniform") {
    uint32_t offset = bindingJson.value("offset", -1);
    uint32_t size = bindingJson.value("size", -1);

    if (isBinding) {
      mBindings.back().size = std::max(mBindings.back().size, offset + size);
    }

    sLogger->trace("    - name: {}", name);
    sLogger->trace("    - offset: {}", offset);
    sLogger->trace("    - size: {}", size);
  }

  const auto &typeJson = varJson["type"];
  std::string typeKind = typeJson.value("kind", "<unknown_kind>");
  sLogger->trace("    - kind: {}", typeKind);

  if (typeKind == "scalar") {
    std::string scalarType =
        typeJson.value("scalarType", "<uknown_scalar_type>");

    if (isNewBinding) {
      mBindings.back().type = BindingType::eUniformBuffer;
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
      mBindings.back().type = BindingType::eUniformBuffer;
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
      mBindings.back().type = BindingType::eUniformBuffer;
      mBindings.back().elementSize = rowCount * colCount * 4;
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
        mBindings.back().type = BindingType::eUniformBuffer;
      }

      for (const auto &fieldJson : elementTypeJson["fields"]) {
        parseJsonRecursive(fieldJson, isNewBinding);
      }
    }

    if (elementKind == "array") {

      const auto &arrayElementType = elementTypeJson["elementType"];
      for (const auto &field : arrayElementType["fields"]) {
        parseJsonRecursive(field, isNewBinding);
      }

      if (isNewBinding) {
        mBindings.back().type = BindingType::eUniformBuffer;
        mBindings.back().size *=
            (uint32_t)elementTypeJson.value("elementCount", 0);
      }
    }

    return;
  }

  if (typeKind == "samplerState") {
    if (isNewBinding) {
      mBindings.back().type = BindingType::eSampler;
    }

    return;
  }

  if (typeKind == "resource") {
    std::string baseShape = typeJson.value("baseShape", "<unknown_base_shape>");

    if (isNewBinding) {
      if (baseShape == "structuredBuffer") {
        mBindings.back().type = BindingType::eStorageBuffer;
        auto resultTypeJson = typeJson["resultType"];

        std::string resultTypeKind =
            resultTypeJson.value("kind", "<uknown_type_kind>");

        if (resultTypeKind == "struct") {
          auto resultTypeFields = resultTypeJson["fields"];
          for (const auto &field : resultTypeFields) {
            parseJsonRecursive(field, true);
          }
        } else {
          sLogger->error("Only StructuredBuffer<StructType> is supported!");
          return;
        };
      }

      if (baseShape == "texture2D") {
        mBindings.back().type = BindingType::eTexture2D;
      }
    }

    sLogger->trace("    - resouceShape: {}", baseShape);
    return;
  }

  if (typeKind == "struct") {
    for (const auto &fieldJson : typeJson["fields"]) {
      parseJsonRecursive(fieldJson, false);
    }
  }

  sLogger->error("Cubozoa does not currently support {}!", typeKind);
}

Result ShaderWebGPU::create(const std::string &path) {
  std::filesystem::path shaderPath = path;

  std::filesystem::path shaderReflectionPath = shaderPath;
  shaderReflectionPath.replace_extension("json");

  sLogger->trace("{}", shaderPath.c_str());
  sLogger->trace("{}", shaderReflectionPath.c_str());

  std::ifstream reflectionFile(shaderReflectionPath);

  if (!reflectionFile.is_open()) {
    return Result::eFailure;
  }

  // Parse uniforms
  nlohmann::json json = nlohmann::json::parse(reflectionFile);
  for (const auto &paramJson : json["parameters"]) {
    parseJsonRecursive(paramJson, -1);
  }

  // Vertex Input scope
  for (const auto &entryPoint : json["entryPoints"]) {
    if (entryPoint.value("stage", "") != "fragment") {
      mStages |= WGPUShaderStage_Fragment;
    }

    if (entryPoint.value("stage", "") != "compute") {
      mStages |= WGPUShaderStage_Compute;
    }

    if (entryPoint.value("stage", "") != "vertex") {
      continue;
    }
    mStages |= WGPUShaderStage_Vertex;

    mVertexLayout.begin(VertexStepMode::eVertex);
    for (const auto &param : entryPoint["parameters"]) {
      auto fields = param["type"]["fields"];
      for (const auto &field : fields) {
        // Skipped built in
        std::string semanticName = field.value("semanticName", "");
        if (semanticName == "SV_VULKANINSTANCEID") {
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
    break;
  }

  reflectionFile.close();

  std::string shaderSrcCode;
  if (LoadFileAsText(shaderPath.string(), shaderSrcCode) != Result::eSuccess) {
    return Result::eWGPUError;
  }
  WGPUShaderModuleDescriptor shaderModuleDesc{};

  WGPUShaderModuleWGSLDescriptor shaderCodeDesc;
  shaderCodeDesc.chain.next = nullptr;
  shaderCodeDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
  shaderCodeDesc.code = shaderSrcCode.c_str();

  shaderModuleDesc.nextInChain = &shaderCodeDesc.chain;
  shaderModuleDesc.label = path.c_str();
#ifdef WEBGPU_BACKEND_WGPU
  shaderModuleDesc.hintCount = 0;
  shaderModuleDesc.hints = nullptr;
#endif

  mModule = wgpuDeviceCreateShaderModule(sDevice, &shaderModuleDesc);

  return Result::eSuccess;
}

void ShaderWebGPU::destroy() { wgpuShaderModuleRelease(mModule); }

Result GraphicsProgramWebGPU::create(const ShaderWebGPU *shader,
                                     VertexLayout *requestedVertexLayout,
                                     const std::string &name) {
  if (!shader) {
    spdlog::error("Attempting to create graphics pipeline with null shader!");
    return Result::eWGPUError;
  }
  mShader = shader;

  WGPURenderPipelineDescriptor pipelineDesc = {};
  pipelineDesc.nextInChain = nullptr;
  pipelineDesc.label = name.c_str();

  std::vector<WGPUBindGroupLayoutEntry> bindingEntries(
      mShader->getBindings().size());

  for (size_t i = 0; i < bindingEntries.size(); i++) {
    bindingEntries[i] = {};
    bindingEntries[i].binding = i;
    bindingEntries[i].visibility = mShader->getShaderStages();
    switch (mShader->getBindings()[i].type) {
    case BindingType::eUniformBuffer:
      bindingEntries[i].buffer.type = WGPUBufferBindingType_Uniform;
      bindingEntries[i].buffer.nextInChain = nullptr;
      bindingEntries[i].buffer.hasDynamicOffset = false;
      bindingEntries[i].buffer.minBindingSize = mShader->getBindings()[i].size;
      break;
    case BindingType::eStorageBuffer:
      bindingEntries[i].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
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
      sLogger->error("Unsupported binding type {}",
                     (uint32_t)mShader->getBindings()[i].type);
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

  std::string layoutName = std::string(name) + std::string("_layout");
  WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
  pipelineLayoutDesc.nextInChain = nullptr;
  pipelineLayoutDesc.label = layoutName.c_str();
  pipelineLayoutDesc.bindGroupLayoutCount = 1;
  pipelineLayoutDesc.bindGroupLayouts = &mBindGroupLayout;
  mPipelineLayout =
      wgpuDeviceCreatePipelineLayout(sDevice, &pipelineLayoutDesc);
  pipelineDesc.layout = mPipelineLayout;

  if (requestedVertexLayout) {
  }

  WGPUVertexBufferLayout vertexBufferLayout = {};
  vertexBufferLayout.arrayStride = mShader->getVertexLayout().stride;
  vertexBufferLayout.stepMode =
      static_cast<WGPUVertexStepMode>(mShader->getVertexLayout().stepMode);
  vertexBufferLayout.attributeCount =
      mShader->getVertexLayout().attributes.size();
  vertexBufferLayout.attributes =
      (WGPUVertexAttribute const *)(mShader->getVertexLayout()
                                        .attributes.data());

  WGPUVertexState vertexState = {};
  vertexState.nextInChain = nullptr;
  vertexState.module = mShader->getModule();
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
  if ((mShader->getShaderStages() & WGPUShaderStage_Fragment) ==
      WGPUShaderStage_Fragment) {
    fragmentState.entryPoint = "fragmentMain";
    fragmentState.constantCount = 0;
    fragmentState.constants = nullptr;

    fragmentState.targetCount = 1;
    fragmentState.targets = &targetState;
    fragmentState.module = mShader->getModule();
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
}

Result RendererContextWebGPU::init(uint32_t width, uint32_t height, void *nwh) {
  sLogger = spdlog::stdout_color_mt("wgpu");
  sLogger->set_level(spdlog::level::trace);
  sLogger->set_pattern("[%^%l%$][CBZ|RENDERER|WGPU] %v");

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
  for (WGPUFeatureName _ : adapterFeatures) {
  }

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

void RendererContextWebGPU::drawSorted(
    const std::vector<RenderCommand> &sortedDraws) {
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

  WGPURenderPassEncoder renderPassEncoder =
      wgpuCommandEncoderBeginRenderPass(cmdEncoder, &renderPassDesc);

  uint64_t lastRenderCommandID = -1;
  uint32_t vertexCount = 0;

  bool drawIndexed = false;
  uint32_t indexCount = 0;

  for (const RenderCommand &renderCmd : sortedDraws) {
    if (lastRenderCommandID != renderCmd.sortKey) {
      lastRenderCommandID = renderCmd.sortKey;
      if (mGraphicsPrograms[renderCmd.gph.idx].getShader()->getVertexLayout() !=
          mVertexBuffers[renderCmd.vbh.idx].getVertexLayout()) {
        sLogger->warn("Incompatible vertex buffer and program layout!");
        sLogger->warn("Discarding draw...");
        continue;
      }

      const GraphicsProgramWebGPU &graphicsProgram =
          mGraphicsPrograms[renderCmd.gph.idx];

      if (graphicsProgram.bind(renderPassEncoder) != Result::eSuccess) {
        continue;
      }

      // Find/Create and set bindingGroup
      if (mBindingGroups.find(renderCmd.getDescriptorHash()) ==
          mBindingGroups.end()) {

        WGPUBindGroupDescriptor bindGroupDesc = {};
        bindGroupDesc.nextInChain = nullptr;
        bindGroupDesc.label = nullptr;
        bindGroupDesc.layout =
            mGraphicsPrograms[renderCmd.gph.idx].getBindGroupLayout();

        std::vector<WGPUBindGroupEntry> bindGroupEntries(
            renderCmd.uniformCount);

        const std::vector<Binding> &shaderBindings =
            graphicsProgram.getShader()->getBindings();

        for (uint32_t uIdx = 0; uIdx < renderCmd.uniformCount; uIdx++) {
          bindGroupEntries[uIdx].binding = uIdx;

          switch (renderCmd.uniforms[uIdx].type) {
          case UniformType::eVec4:
          case UniformType::eMat4: {
            UniformHandle uh = renderCmd.uniforms[uIdx].uniformBuffer.handle;
            const auto &it = std::find_if(
                shaderBindings.begin(), shaderBindings.end(),
                [=](const Binding &binding) {
                  return binding.name ==
                         HandleProvider<UniformHandle>::getName(uh);
                });

            if (it == shaderBindings.end()) {
              sLogger->error(
                  "Program {} has no uniform binding named {}",
                  HandleProvider<GraphicsProgramHandle>::getName(renderCmd.gph),
                  HandleProvider<UniformHandle>::getName(uh));
              continue;
            }

            bindGroupEntries[uIdx] =
                mUniformBuffers[uh.idx].createBindGroupEntry(uIdx);
          } break;
          case UniformType::eTexture2D: {
            TextureHandle th = renderCmd.uniforms[uIdx].texture.handle;

            const auto &it = std::find_if(
                shaderBindings.begin(), shaderBindings.end(),
                [=](const Binding &binding) {
                  return binding.name ==
                         HandleProvider<TextureHandle>::getName(th);
                });

            if (it->type != BindingType::eTexture2D) {
              sLogger->error(
                  "Program {} has uniform binding and type mismatch for '{}'",
                  HandleProvider<GraphicsProgramHandle>::getName(renderCmd.gph),
                  HandleProvider<TextureHandle>::getName(th));
              continue;
            }

            if (it == shaderBindings.end()) {
              sLogger->error(
                  "Program {} has no uniform binding named {}",
                  HandleProvider<GraphicsProgramHandle>::getName(renderCmd.gph),
                  HandleProvider<TextureHandle>::getName(th));
              continue;
            }

            bindGroupEntries[uIdx] =
                mTextures[th.idx].createBindGroupEntry(uIdx);
          } break;
          case UniformType::eSampler: {
            SamplerHandle sh = renderCmd.uniforms[uIdx].texture.samplerHandle;

            const auto &it =
                std::find_if(shaderBindings.begin(), shaderBindings.end(),
                             [=](const Binding &binding) {
                               return binding.name == "albedoSampler";
                             });

            if (it->type != BindingType::eSampler) {
              sLogger->error(
                  "Program {} has uniform binding and type mismatch for",
                  HandleProvider<GraphicsProgramHandle>::getName(
                      renderCmd.gph));
              continue;
            }

            if (it == shaderBindings.end()) {
              sLogger->error(
                  "Program {} has no uniform binding named {}",
                  HandleProvider<GraphicsProgramHandle>::getName(renderCmd.gph),
                  "albedoSampler");
              continue;
            }

            WGPUBindGroupEntry entry = {};
            entry.binding = uIdx;
            entry.nextInChain = nullptr;
            entry.sampler = mSamplers[sh.idx];
            bindGroupEntries[uIdx] = entry;
          } break;
          default:
            break;
          }
        }

        bindGroupDesc.entryCount = renderCmd.uniformCount;
        bindGroupDesc.entries = bindGroupEntries.data();

        mBindingGroups[renderCmd.getDescriptorHash()] =
            wgpuDeviceCreateBindGroup(sDevice, &bindGroupDesc);
      }

      uint32_t offsets = 0;
      wgpuRenderPassEncoderSetBindGroup(
          renderPassEncoder, 0, mBindingGroups[renderCmd.getDescriptorHash()],
          0, &offsets);

      if (renderCmd.vbh.idx != CBZ_INVALID_HANDLE) {
        if (mVertexBuffers[renderCmd.vbh.idx].bind(renderPassEncoder) !=
            Result::eSuccess) {
          continue;
        };
        vertexCount = mVertexBuffers[renderCmd.vbh.idx].getVertexCount();
      }

      if (renderCmd.ibh.idx != CBZ_INVALID_HANDLE) {
        if (mIndexBuffers[renderCmd.ibh.idx].bind(renderPassEncoder) !=
            Result::eSuccess) {
          continue;
        }

        indexCount = mIndexBuffers[renderCmd.ibh.idx].getIndexCount();
        drawIndexed = true;
      } else {
        indexCount = 0;
        drawIndexed = false;
      }
    }

    if (drawIndexed) {
      wgpuRenderPassEncoderDrawIndexed(renderPassEncoder, indexCount, 1, 0, 0,
                                       0);
    } else {
      wgpuRenderPassEncoderDraw(renderPassEncoder, vertexCount, 1, 0, 0);
    }
  }

  ImGui_ImplWGPU_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  ImGui::Begin("Hey");
  static float vec[3];
  ImGui::SliderFloat3("Color", vec, 0.0f, 100.0f);
  ImGui::End();

  ImGui::EndFrame();
  ImGui::Render();
  ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), renderPassEncoder);

  wgpuRenderPassEncoderEnd(renderPassEncoder);
  wgpuRenderPassEncoderRelease(renderPassEncoder);

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
}

Result
RendererContextWebGPU::vertexBufferCreate(VertexBufferHandle vbh,
                                          const VertexLayout &vertexLayout,
                                          uint32_t count, const void *data) {
  if (mVertexBuffers.size() < vbh.idx + 1) {
    mVertexBuffers.resize(vbh.idx + 1);
  }

  return mVertexBuffers[vbh.idx].create(vertexLayout, count, data);
}

void RendererContextWebGPU::vertexBufferDestroy(VertexBufferHandle vbh) {
  return mVertexBuffers[vbh.idx].destroy();
}

Result RendererContextWebGPU::indexBufferCreate(IndexBufferHandle ibh,
                                                IndexFormat format,
                                                uint32_t count,
                                                const void *data) {
  if (mIndexBuffers.size() < ibh.idx + 1) {
    mIndexBuffers.resize(ibh.idx + 1);
  }

  return mIndexBuffers[ibh.idx].create(static_cast<WGPUIndexFormat>(format),
                                       count, data);
}

void RendererContextWebGPU::indexBufferDestroy(IndexBufferHandle ibh) {
  return mIndexBuffers[ibh.idx].destroy();
}

Result RendererContextWebGPU::uniformBufferCreate(UniformHandle uh,
                                                  UniformType type,
                                                  uint16_t num,
                                                  const void *data) {
  if (mUniformBuffers.size() < uh.idx + 1) {
    mUniformBuffers.resize(uh.idx + 1);
  }

  return mUniformBuffers[uh.idx].create(
      type, num, data, HandleProvider<UniformHandle>::getName(uh));
}

void RendererContextWebGPU::uniformBufferUpdate(UniformHandle uh, void *data,
                                                uint32_t num) {
  mUniformBuffers[uh.idx].update(data, num);
}

void RendererContextWebGPU::uniformBufferDestroy(UniformHandle uh) {
  return mUniformBuffers[uh.idx].destroy();
}

SamplerHandle
RendererContextWebGPU::getSampler(TextureBindingDesc texBindingDesc) {
  uint32_t samplerID;
  MurmurHash3_x86_32(&texBindingDesc, sizeof(texBindingDesc), 0, &samplerID);

  if (mSamplers.find(samplerID) != mSamplers.end()) {
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
  mSamplers[samplerID] = sampler;
  return SamplerHandle{samplerID};
};

Result RendererContextWebGPU::textureCreate(TextureHandle th,
                                            TextureFormat format, uint32_t w,
                                            uint32_t h, uint32_t depth,
                                            TextureDimension dimension) {
  if (mTextures.size() < th.idx + 1) {
    mTextures.resize(th.idx + 1);
  }

  return mTextures[th.idx].create(w, h, depth,
                                  TextureDimToWGPU(dimension),
                                  static_cast<WGPUTextureFormat>(format));
}

void RendererContextWebGPU::textureUpdate(TextureHandle th, void *data,
                                          uint32_t count) {
  mTextures[th.idx].update(data, count);
};

void RendererContextWebGPU::textureDestroy(TextureHandle th) {
  return mTextures[th.idx].destroy();
};

Result RendererContextWebGPU::shaderCreate(ShaderHandle sh,
                                           const std::string &path) {
  if (mShaders.size() < sh.idx + 1) {
    mShaders.resize(sh.idx + 1);
  }

  return mShaders[sh.idx].create(path);
}

void RendererContextWebGPU::shaderDestroy(ShaderHandle sh) {
  return mShaders[sh.idx].destroy();
}

Result RendererContextWebGPU::graphicsProgramCreate(GraphicsProgramHandle gph,
                                                    ShaderHandle sh) {
  if (mGraphicsPrograms.size() < gph.idx + 1) {
    mGraphicsPrograms.resize(gph.idx + 1);
  }

  return mGraphicsPrograms[gph.idx].create(&mShaders[sh.idx]);
}

void RendererContextWebGPU::graphicsProgramDestroy(GraphicsProgramHandle gph) {
  return mGraphicsPrograms[gph.idx].destroy();
}

void RendererContextWebGPU::shutdown() {
  ImGui_ImplGlfw_Shutdown();
  ImGui_ImplWGPU_Shutdown();

  wgpuSurfaceRelease(sSurface);
  wgpuDeviceRelease(sDevice);
}

} // namespace cbz

std::unique_ptr<cbz::IRendererContext> RendererContextCreate() {
  return std::make_unique<cbz::RendererContextWebGPU>();
}
