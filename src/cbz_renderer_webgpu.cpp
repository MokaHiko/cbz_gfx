#include "cbz_renderer_webgpu.h"

#include <GLFW/glfw3.h>

#include "cbz_gfx/cbz_gfx_defines.h"
#include "cbz_gfx/net/cbz_net_http.h"
#include "cbz_irenderer_context.h"

#include <cbz/cbz_file.h>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <limits>
#include <murmurhash/MurmurHash3.h>
#include <nlohmann/json.hpp>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_wgpu.h>

#include <fstream>
#include <string>
#include <webgpu/webgpu.h>

constexpr static WGPUTextureDimension
TextureDimToWGPU(CBZTextureDimension dim) {
  switch (dim) {
  case CBZ_TEXTURE_DIMENSION_1D:
    return WGPUTextureDimension_1D;
  case CBZ_TEXTURE_DIMENSION_2D:
    return WGPUTextureDimension_2D;
  case CBZ_TEXTURE_DIMENSION_3D:
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
static cbz::ImageHandle sSurfaceIMGH;

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

// --- ImGui ---
#include "cbz_gfx/cbz_gfx_imgui.h"
static CBZ_ImGuiRenderFunc sImguiRenderfunc = nullptr;

namespace cbz::imgui {

void Image(cbz::ImageHandle imgh, const ImVec2 &size, const ImVec2 &uv0,
           const ImVec2 &uv1, const ImVec4 &tint_col,
           const ImVec4 &border_col) {
  ImGui::Image(
      sTextures[imgh.idx].findOrCreateTextureView(WGPUTextureAspect_All), size,
      uv0, uv1, tint_col, border_col);
}

}; // namespace cbz::imgui

namespace cbz {

void SetImGuiRenderCallback(CBZ_ImGuiRenderFunc func) {
  sImguiRenderfunc = func;
}

}; // namespace cbz

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
  requiredLimits.limits.maxVertexBuffers = cbz::MAX_VERTEX_INPUT_BINDINGS;
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
      static_cast<uint32_t>(CBZ_BUFFER_COUNT);
  requiredLimits.limits.maxUniformBufferBindingSize = 65536; // Default
  requiredLimits.limits.maxStorageBuffersPerShaderStage =
      static_cast<uint32_t>(CBZ_BUFFER_COUNT);
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

// TODO: Add offset
static void AlignedWriteBufferWGPU(WGPUBuffer buffer, const void *data,
                                   uint32_t size) {
  // Split write if not aligned
  uint32_t misalignedSize = size % 4;
  assert(size > 3);

  if (misalignedSize > 0) {
    uint32_t allignedSize = size - misalignedSize;
    wgpuQueueWriteBuffer(sQueue, buffer, 0, data, allignedSize);

    std::array<uint32_t, 4> misalignedData;
    memcpy(misalignedData.data(),
           static_cast<const uint8_t *>(data) + allignedSize, misalignedSize);
    wgpuQueueWriteBuffer(sQueue, buffer, allignedSize, misalignedData.data(),
                         4);
  } else {
    wgpuQueueWriteBuffer(sQueue, buffer, 0, data, size);
  }
};

namespace cbz {

class RendererContextWebGPU : public IRendererContext {
public:
  Result init(uint32_t width, uint32_t height, void *nwh,
              ImageHandle swapchainIMGH) override;

  [[nodiscard]] Result vertexBufferCreate(VertexBufferHandle vbh,
                                          const VertexLayout &vertexLayout,
                                          uint32_t count,
                                          const void *data) override;

  void vertexBufferUpdate(VertexBufferHandle vbh, uint32_t elementCount,
                          const void *data, uint32_t elementOffset) override {
    sVertexBuffers[vbh.idx].update(data, elementCount, elementOffset);
  }

  void vertexBufferDestroy(VertexBufferHandle vbh) override;

  [[nodiscard]] Result indexBufferCreate(IndexBufferHandle ibh,
                                         CBZIndexFormat format, uint32_t size,
                                         const void *data = nullptr) override;

  void indexBufferDestroy(IndexBufferHandle ibh) override;

  [[nodiscard]] Result uniformBufferCreate(UniformHandle uh,
                                           CBZUniformType type, uint16_t num,
                                           const void *data = nullptr) override;

  void uniformBufferUpdate(UniformHandle uh, const void *data,
                           uint16_t num) override;

  void uniformBufferDestroy(UniformHandle uh) override;

  [[nodiscard]] Result structuredBufferCreate(StructuredBufferHandle sbh,
                                              CBZUniformType type, uint32_t num,
                                              const void *data,
                                              int flags) override;

  void structuredBufferUpdate(StructuredBufferHandle sbh, uint32_t elementCount,
                              const void *data,
                              uint32_t elementOffset) override;

  void structuredBufferDestroy(StructuredBufferHandle sbh) override;

  [[nodiscard]] SamplerHandle
  getSampler(TextureBindingDesc texBindingDesc) override;

  [[nodiscard]] Result imageCreate(ImageHandle th, CBZTextureFormat format,
                                   uint32_t w, uint32_t h, uint32_t depth,
                                   CBZTextureDimension dimension,
                                   CBZImageFlags flags) override;

  void imageUpdate(ImageHandle th, void *data, uint32_t count) override;

  void imageDestroy(ImageHandle th) override;

  [[nodiscard]] Result shaderCreate(ShaderHandle sh, CBZShaderFlags flags,
                                    const std::string &path) override;

  void shaderDestroy(ShaderHandle sh) override;

  [[nodiscard]] Result graphicsProgramCreate(GraphicsProgramHandle gph,
                                             ShaderHandle sh,
                                             int flags) override;

  void graphicsProgramDestroy(GraphicsProgramHandle gph) override;

  [[nodiscard]] Result computeProgramCreate(ComputeProgramHandle cph,
                                            ShaderHandle sh) override;

  void computeProgramDestroy(ComputeProgramHandle cph) override;

  void
  readBufferAsync(StructuredBufferHandle sbh,
                  std::function<void(const void *data)> callback) override {
    struct BufferReadRequest {
      StructuredBufferHandle sbh;
      uint32_t frameFinished;
      cbz::Result result;
      std::function<void(const void *data)> callback;
    } bufferReadRequest = {};

    bufferReadRequest.callback = callback;
    bufferReadRequest.sbh = sbh;

    wgpuBufferMapAsync(
        sStorageBuffers[sbh.idx].mBuffer, WGPUBufferUsage_MapRead, 0,
        sStorageBuffers[sbh.idx].getSize(),
        [](WGPUBufferMapAsyncStatus status, void *userdata) {
          BufferReadRequest *request =
              static_cast<BufferReadRequest *>(userdata);
          // request->frameFinished = mFrameCounter;

          switch (status) {
          case WGPUBufferMapAsyncStatus_Success: {
            const void *data = wgpuBufferGetConstMappedRange(
                sStorageBuffers[request->sbh.idx].mBuffer, 0,
                sStorageBuffers[request->sbh.idx].getSize());

            if (request->callback) {
              request->callback(data);
            };

            wgpuBufferUnmap(sStorageBuffers[request->sbh.idx].mBuffer);
            request->result = Result::eSuccess;
          } break;

          default: {
            request->result = Result::eFailure;
            sLogger->error("Failed to read buffer {:X}!", (int)status);
          } break;
          };
        },
        &bufferReadRequest);
  }

  void
  textureReadAsync(ImageHandle imgh, const Origin3D *origin,
                   const TextureExtent *extent,
                   std::function<void(const void *data)> callback) override {
    const size_t textureSize =
        sTextures[imgh.idx].getExtent().width *
        sTextures[imgh.idx].getExtent().height *
        TextureFormatGetSize(
            static_cast<CBZTextureFormat>(sTextures[imgh.idx].getFormat()));

    const size_t textureAreaSize =
        extent->width * extent->height *
        TextureFormatGetSize(
            static_cast<CBZTextureFormat>(sTextures[imgh.idx].getFormat()));

    WGPUExtent3D extent3D = {};
    extent3D.width = extent->width;
    extent3D.height = extent->height;
    extent3D.depthOrArrayLayers = extent->layers;

    WGPUOrigin3D origin3D = {};
    origin3D.x = origin->x;
    origin3D.y = origin->y;
    origin3D.z = origin->z;

    WGPUBuffer tempStagingBuffer =
        getTransientDestinationBuffer(textureAreaSize);
    copyTextureToBuffer(sTextures[imgh.idx].mTexture, &origin3D,
                        tempStagingBuffer, &extent3D);

    const size_t textureBufferOffset =
        (origin->y * sTextures[imgh.idx].getExtent().width + origin->x) *
        TextureFormatGetSize(
            static_cast<CBZTextureFormat>(sTextures[imgh.idx].getFormat()));

    if (textureBufferOffset + textureAreaSize > textureSize) {
      spdlog::warn("Overflow! Attempting to read beyond {}!", textureSize);
      spdlog::warn("Discarding texture read origin: {} {} {} extent {} {} {}!",
                   origin->x, origin->y, origin->z, extent->width,
                   extent->height, extent->layers);
      return;
    }

    // TODO: Store/Keep alive in request queue
    static struct TextureReadRequest {
      uint32_t frameFinished;
      WGPUBuffer stagingBuffer;
      size_t textureSize;
      std::function<void(const void *data)> callback;
      cbz::Result result;
    } textureReadRequest = {};

    textureReadRequest.textureSize = textureAreaSize;
    textureReadRequest.callback = callback;
    textureReadRequest.stagingBuffer = tempStagingBuffer;

    wgpuBufferMapAsync(
        tempStagingBuffer, WGPUBufferUsage_MapRead, textureBufferOffset,
        textureAreaSize,
        [](WGPUBufferMapAsyncStatus status, void *userdata) {
          TextureReadRequest *request =
              static_cast<TextureReadRequest *>(userdata);
          // request->frameFinished = mFrameCounter;

          switch (status) {
          case WGPUBufferMapAsyncStatus_Success: {
            const void *data = wgpuBufferGetConstMappedRange(
                request->stagingBuffer, 0, request->textureSize);

            if (request->callback) {
              request->callback(data);
            };

            wgpuBufferUnmap(request->stagingBuffer);
            request->result = Result::eSuccess;
          } break;

          default: {
            request->result = Result::eFailure;
            sLogger->error("Failed to read buffer 0x{:X}!", (int)status);
          } break;
          };
        },
        &textureReadRequest);

    PollEvents(false);
  }

  uint32_t submitSorted(const std::vector<RenderTarget> &renderTargets,
                        const ShaderProgramCommand *sortedCmds,
                        uint32_t count) override;

  void shutdown() override;

private:
  void copyTextureToBuffer(WGPUTexture src, const WGPUOrigin3D *origin,
                           WGPUBuffer dst, const WGPUExtent3D *extent) {
    WGPUImageCopyTexture srcTexture = {};
    srcTexture.nextInChain = nullptr;
    srcTexture.texture = src;
    srcTexture.mipLevel = 0;
    srcTexture.origin = {origin->x, origin->y, origin->z};
    srcTexture.aspect = WGPUTextureAspect_All;

    const uint32_t textureFormatSize = TextureFormatGetSize(
        static_cast<CBZTextureFormat>(wgpuTextureGetFormat(src)));

    WGPUImageCopyBuffer dstBuffer = {};
    dstBuffer.nextInChain = nullptr;
    dstBuffer.layout = {};
    dstBuffer.layout.nextInChain = nullptr;
    dstBuffer.layout.offset = {};
    dstBuffer.layout.bytesPerRow = extent->width * textureFormatSize;
    dstBuffer.layout.rowsPerImage = extent->height;
    dstBuffer.buffer = dst;

    WGPUCommandEncoderDescriptor descriptor = {};
    WGPUCommandEncoder cmdEncoder =
        wgpuDeviceCreateCommandEncoder(sDevice, &descriptor);
    wgpuCommandEncoderCopyTextureToBuffer(cmdEncoder, &srcTexture, &dstBuffer,
                                          extent);

    WGPUCommandBufferDescriptor cmdDesc = {};
    cmdDesc.nextInChain = nullptr;
    std::string commandBufferLabel = "CopyCommandBuffer";
    cmdDesc.label = commandBufferLabel.c_str();
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(cmdEncoder, &cmdDesc);
    wgpuCommandEncoderRelease(cmdEncoder);

    wgpuQueueSubmit(sQueue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
  }
  // TODO: Create multiple staging buffers for different uses.
  [[nodiscard]] WGPUBuffer getTransientDestinationBuffer(size_t len,
                                                         void *data = nullptr) {
    // Create if non. Resize of lesss
    if (!mStagingBuffer || wgpuBufferGetSize(mStagingBuffer) < len) {
      if (mStagingBuffer) {
        wgpuBufferDestroy(mStagingBuffer);
        mStagingBuffer = nullptr;
      }

      WGPUBufferDescriptor bufferDesc = {};
      bufferDesc.nextInChain = nullptr;
      // bufferDesc.label = name.c_str(); // TODO: Name for use case
      bufferDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
      bufferDesc.size = (len + 3) & ~3; // Pad to multiple of 4
      bufferDesc.mappedAtCreation = false;

      mStagingBuffer = wgpuDeviceCreateBuffer(sDevice, &bufferDesc);
      if (!mStagingBuffer) {
        spdlog::error("Failed to create stagingBuffer!");
        return nullptr;
      }

      if (data) {
        AlignedWriteBufferWGPU(mStagingBuffer, data,
                               static_cast<uint32_t>(len));
      }

      sLogger->trace("Staging buffer resized to {}", len);
    }

    return mStagingBuffer;
  }

  [[nodiscard]] WGPUBindGroup findOrCreateBindGroup(ShaderHandle sh,
                                                    uint32_t descriptorHash,
                                                    const Binding *bindings,
                                                    uint32_t bindingCount);

  WGPUBuffer mStagingBuffer = NULL;
  uint32_t mFrameCounter = 0;
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
    AlignedWriteBufferWGPU(mBuffer, data, size);
  }

  return Result::eSuccess;
};

void VertexBufferWebGPU::update(const void *data, uint32_t elementCount,
                                [[maybe_unused]] uint32_t elementOffset) {
  uint32_t size = elementCount * mVertexLayout.stride;

  if (data) {
    AlignedWriteBufferWGPU(mBuffer, data, size);
  }
}

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
    AlignedWriteBufferWGPU(mBuffer, data, size);
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

[[nodiscard]] Result UniformBufferWebWGPU::create(CBZUniformType type,
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
    AlignedWriteBufferWGPU(mBuffer, data, size);
  }

  return Result::eSuccess;
}

void UniformBufferWebWGPU::update(const void *data, uint16_t num) {
  uint32_t size = UniformTypeGetSize(mElementType) * num;

  if (num == 0) {
    size = UniformTypeGetSize(mElementType) * mElementCount;
  }

  AlignedWriteBufferWGPU(mBuffer, data, size);
}

void UniformBufferWebWGPU::destroy() {
  if (!mBuffer) {
    spdlog::warn("Attempting to destroy invalid uniform buffer");
    return;
  }

  wgpuBufferDestroy(mBuffer);
  mBuffer = NULL;
}

Result StorageBufferWebWGPU::create(CBZUniformType type, uint32_t elementCount,
                                    const void *data,
                                    WGPUBufferUsageFlags usage,
                                    const std::string &name) {
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
  // TODO : Remove copy dst copy src
  bufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst |
                     WGPUBufferUsage_CopySrc | usage;
  bufferDesc.size = size;
  bufferDesc.mappedAtCreation = false;

  mBuffer = wgpuDeviceCreateBuffer(sDevice, &bufferDesc);
  if (!mBuffer) {
    spdlog::error("Failed to create Uniform buffer!");
    return Result::eWGPUError;
  }

  if (data) {
    AlignedWriteBufferWGPU(mBuffer, data, size);
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

  // TODO: Aligned write w offset
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
                             WGPUTextureUsageFlags usage,
                             const std::string &name) {
  WGPUTextureDescriptor textDesc = {};
  textDesc.nextInChain = nullptr;
  textDesc.label = name.c_str();

  textDesc.usage =
      WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding | usage;
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

Result TextureWebGPU::create(WGPUTexture texture) {
  mTexture = texture;
  return Result::eSuccess;
}

void TextureWebGPU::update(void *data, uint32_t count) {
  const uint32_t formatSize = TextureFormatGetSize(
      static_cast<CBZTextureFormat>(wgpuTextureGetFormat(mTexture)));
  const uint32_t size = formatSize * count;

  WGPUImageCopyTexture destination = {};
  destination.nextInChain = nullptr;
  destination.texture = mTexture;
  destination.mipLevel = 0;
  destination.origin = {0, 0, 0};

  switch (getFormat()) {
  case WGPUTextureFormat_Depth16Unorm:
  case WGPUTextureFormat_Depth24Plus:
  case WGPUTextureFormat_Depth24PlusStencil8:
  case WGPUTextureFormat_Depth32Float:
  case WGPUTextureFormat_Depth32FloatStencil8: {
    destination.aspect = WGPUTextureAspect_DepthOnly;
  } break;
  default: {
    destination.aspect = WGPUTextureAspect_All;
  } break;
  }
  destination.aspect = WGPUTextureAspect_All;

  WGPUTextureDataLayout dataLayout = {};
  dataLayout.nextInChain = nullptr;
  dataLayout.offset = 0;
  dataLayout.bytesPerRow = formatSize * wgpuTextureGetWidth(mTexture);
  dataLayout.rowsPerImage = wgpuTextureGetHeight(mTexture);

  WGPUExtent3D extent = {};
  extent.width = wgpuTextureGetWidth(mTexture);
  extent.height = wgpuTextureGetHeight(mTexture);
  extent.depthOrArrayLayers = wgpuTextureGetDepthOrArrayLayers(mTexture);

  wgpuQueueWriteTexture(sQueue, &destination, data, size, &dataLayout, &extent);
}

WGPUTextureView TextureWebGPU::findOrCreateTextureView(
    WGPUTextureAspect aspect, uint32_t baseArrayLayer, uint32_t arrayLayerCount,
    CBZTextureViewDimension viewDimension) {
  uint32_t textureViewKey[]{static_cast<uint32_t>(aspect), baseArrayLayer,
                            arrayLayerCount};
  uint32_t textureViewHash;
  MurmurHash3_x86_32(textureViewKey, sizeof(textureViewKey), 0,
                     &textureViewHash);

  if (mViews.find(textureViewHash) != mViews.end()) {
    return mViews[textureViewHash];
  }

  WGPUTextureViewDescriptor textureView = {};
  textureView.nextInChain = nullptr;

  textureView.format = wgpuTextureGetFormat(mTexture);

  textureView.aspect = aspect;

  switch (viewDimension) {
  case CBZ_TEXTURE_VIEW_DIMENSION_2D:
    textureView.dimension = WGPUTextureViewDimension_2D;
    break;
  case CBZ_TEXTURE_VIEW_DIMENSION_CUBE:
    textureView.dimension = WGPUTextureViewDimension_Cube;
    break;
  }
  textureView.baseMipLevel = 0;
  textureView.mipLevelCount = 1;
  textureView.baseArrayLayer = baseArrayLayer;
  textureView.arrayLayerCount = arrayLayerCount;
  textureView.aspect = aspect;

  return mViews[textureViewHash] =
             wgpuTextureCreateView(mTexture, &textureView);
}

void TextureWebGPU::destroyTextureViews() {
  for (auto it : mViews) {
    WGPUTextureView view = it.second;
    wgpuTextureViewRelease(view);
  }
}

void TextureWebGPU::destroy() {
  if (!mTexture) {
    sLogger->warn("Attempting to release uninitialized texture!");
    return;
  }

  destroyTextureViews();

  wgpuTextureRelease(mTexture);
  mTexture = NULL;
}

void ShaderWebGPU::parseJsonRecursive(const nlohmann::json &varJson,
                                      bool isBinding, ShaderOffsets offsets) {
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
        uint32_t globalBindingIdx = offsets.bindingOffset + bindingIndex;
        if (globalBindingIdx > std::numeric_limits<uint8_t>::max()) {
          sLogger->error("Binding index out of range {}", globalBindingIdx);
        }

        mBindingDescs.push_back({});
        mBindingDescs.back().index = static_cast<uint8_t>(globalBindingIdx);
        mBindingDescs.back().name = name;

        isNewBinding = true;

        sLogger->trace("binding(@{}): '{}'", mBindingDescs.back().index, name);
      } else {
        sLogger->trace("'{}' contains binding: ", name);
        offsets.bindingOffset = bindingIndex;
      }
    }

    if (bindingKind == "uniform") {
      uint32_t offset = bindingJson.value("offset", -1);
      uint32_t size = bindingJson.value("size", -1);

      if (isBinding) {
        mBindingDescs.back().size =
            std::max(mBindingDescs.back().size, offset + size);
        mBindingDescs.back().padding = offsets.padding;
      }

      sLogger->trace("    - name: {}", name);
      sLogger->trace("    -     offset: {}", offset);
      sLogger->trace("    -     size: {}", size);
      sLogger->trace("    -     padding: {}", offsets.padding);
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
    const auto &elementVarLayout = typeJson["elementVarLayout"];

    std::string elementKind = elementTypeJson.value("kind", "<unknown_kind>");

    sLogger->trace("    - elementKind: {}", elementKind);

    if (elementKind == "struct") {

      if (isNewBinding) {
        mBindingDescs.back().type = BindingType::eUniformBuffer;
      }

      // TODO: Hot garbage make safe.
      for (size_t fieldIdx = 0; fieldIdx < elementTypeJson["fields"].size();
           fieldIdx++) {
        uint32_t offset = (uint32_t)
            elementVarLayout["type"]["fields"][fieldIdx]["binding"]["offset"];
        uint32_t size = (uint32_t)
            elementVarLayout["type"]["fields"][fieldIdx]["binding"]["size"];

        uint32_t nextOffset = 0;
        if (fieldIdx < elementTypeJson["fields"].size() - 1) {

          if ((uint32_t)
                  elementVarLayout["type"]["fields"][fieldIdx + 1]["binding"]
                      .contains("offset")) {
            nextOffset =
                (uint32_t)elementVarLayout["type"]["fields"][fieldIdx + 1]
                                          ["binding"]["offset"];
          } else {
            parseJsonRecursive(elementTypeJson["fields"][fieldIdx],
                               isNewBinding, offsets);
          }
        } else {
          nextOffset = (uint32_t)elementVarLayout["binding"]["size"];
        }

        offsets.padding = nextOffset - (offset + size);
        parseJsonRecursive(elementTypeJson["fields"][fieldIdx], isNewBinding,
                           offsets);

        offsets.padding = 0;
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
        } else if (resultTypeKind == "scalar") {
          parseJsonRecursive(resultTypeJson, true, offsets);
        } else {
          sLogger->error("StructuredBuffer<{}> is not supported!",
                         resultTypeKind);
          return;
        };
      }

      if (baseShape == "texture2D") {
        mBindingDescs.back().type = BindingType::eTexture2D;
      }

      if (baseShape == "textureCube") {
        mBindingDescs.back().type = BindingType::eTextureCube;
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

Result ShaderWebGPU::create(const std::string &path, CBZShaderFlags flags) {
  std::filesystem::path shaderPath = path;
  std::filesystem::path reflectionPath = path;
  reflectionPath.replace_extension(".json");

  if (!std::filesystem::exists(path)) {
    sLogger->critical("No file in path {}!", path);
    return Result::eFailure;
  }

  if (!std::filesystem::exists(reflectionPath)) {
    sLogger->critical("No file in path {}!", reflectionPath.string());
    return Result::eFailure;
  }

  std::ifstream reflectionStream(reflectionPath);
  nlohmann::json reflectionJson = nlohmann::json::parse(reflectionStream);

  // Parse uniforms
  for (const auto &paramJson : reflectionJson["parameters"]) {
    parseJsonRecursive(paramJson, false, {});
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

      mVertexLayout.begin(CBZ_VERTEX_STEP_MODE_VERTEX);
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

          CBZVertexAttributeType attribute =
              static_cast<CBZVertexAttributeType>(location);

          CBZVertexFormat format = CBZ_VERTEX_FORMAT_COUNT;

          if (scalarType == "float32") {
            format = static_cast<CBZVertexFormat>(
                static_cast<uint32_t>(CBZ_VERTEX_FORMAT_FLOAT32) + components -
                1);
          }

          if (scalarType == "uint32") {
            format = static_cast<CBZVertexFormat>(
                static_cast<uint32_t>(CBZ_VERTEX_FORMAT_UINT32) + components -
                1);
          }
          if (scalarType == "sint32") {
            format = static_cast<CBZVertexFormat>(
                static_cast<uint32_t>(CBZ_VERTEX_FORMAT_SINT32) + components -
                1);
          }

          if (format == CBZ_VERTEX_FORMAT_COUNT) {
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

  WGPUShaderModuleDescriptor shaderModuleDesc{};

  shaderModuleDesc.label = path.c_str();
#ifdef WEBGPU_BACKEND_WGPU
  shaderModuleDesc.hintCount = 0;
  shaderModuleDesc.hints = nullptr;
#endif

  if ((flags & CBZ_SHADER_SPIRV) == CBZ_SHADER_SPIRV) {
    std::vector<uint8_t> shaderSrcCode;
    if (LoadFileAsBinary(shaderPath.string(), shaderSrcCode) !=
        Result::eSuccess) {
      return Result::eWGPUError;
    }

    WGPUShaderModuleSPIRVDescriptor shaderCodeDesc = {};
    shaderCodeDesc.chain.next = nullptr;
    shaderCodeDesc.chain.sType = WGPUSType_ShaderModuleSPIRVDescriptor;
    shaderCodeDesc.code =
        reinterpret_cast<const uint32_t *>(shaderSrcCode.data());
    shaderCodeDesc.codeSize =
        static_cast<uint32_t>(shaderSrcCode.size()) / sizeof(uint32_t);
    shaderModuleDesc.nextInChain = &shaderCodeDesc.chain;

    mModule = wgpuDeviceCreateShaderModule(sDevice, &shaderModuleDesc);
    if (!mModule) {
      return Result::eFailure;
    }
  } else {
    std::string shaderSrcCode;
    if (LoadFileAsText(shaderPath.string(), shaderSrcCode) !=
        Result::eSuccess) {
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
    shaderModuleDesc.nextInChain = &shaderCodeDesc.chain;

    mModule = wgpuDeviceCreateShaderModule(sDevice, &shaderModuleDesc);
    if (!mModule) {
      return Result::eFailure;
    }
  }

  return Result::eSuccess;
}

WGPUBindGroupLayout
ShaderWebGPU::findOrCreateBindGroupLayout(const Binding *bindings,
                                          uint32_t bindingCount) {
  uint32_t hash;
  MurmurHash3_x86_32(bindings, sizeof(Binding) * bindingCount, 0, &hash);

  if (mBindGroupLayouts.find(hash) != mBindGroupLayouts.end()) {
    return mBindGroupLayouts[hash];
  }

  std::vector<WGPUBindGroupLayoutEntry> bindingEntries(getBindings().size());

  for (size_t i = 0; i < bindingEntries.size(); i++) {
    bindingEntries[i] = {};
    bindingEntries[i].binding = mBindingDescs[i].index;
    bindingEntries[i].visibility = getShaderStages();

    const BindingDesc &bindingDesc = getBindings()[i];
    switch (bindingDesc.type) {
    case BindingType::eUniformBuffer:
      bindingEntries[i].buffer.type = WGPUBufferBindingType_Uniform;
      bindingEntries[i].buffer.nextInChain = nullptr;
      bindingEntries[i].buffer.hasDynamicOffset = false;
      bindingEntries[i].buffer.minBindingSize =
          bindingDesc.size + bindingDesc.padding;
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

    case BindingType::eTexture2D: {
      bindingEntries[i].texture.nextInChain = nullptr;
      bindingEntries[i].texture.viewDimension = WGPUTextureViewDimension_2D;
      for (uint32_t bindingIndex = 0; bindingIndex < bindingCount;
           bindingIndex++) {
        if (bindings[bindingIndex].type != BindingType::eTexture2D ||
            bindings[bindingIndex].value.texture.slot != bindingDesc.index) {
          continue;
        }

        switch (sTextures[bindings[bindingIndex].value.texture.handle.idx]
                    .getFormat()) {
        case WGPUTextureFormat_Depth16Unorm:
        case WGPUTextureFormat_Depth24Plus:
        case WGPUTextureFormat_Depth24PlusStencil8:
        case WGPUTextureFormat_Depth32Float:
        case WGPUTextureFormat_Depth32FloatStencil8: {
          bindingEntries[i].texture.sampleType = WGPUTextureSampleType_Depth;
          bindingEntries[i].texture.sampleType = WGPUTextureSampleType_Float;
        } break;

        default: {
          bindingEntries[i].texture.sampleType = WGPUTextureSampleType_Float;
        } break;
        }

        break;
      }
    } break;

    case BindingType::eTextureCube: {
      bindingEntries[i].texture.nextInChain = nullptr;
      bindingEntries[i].texture.viewDimension = WGPUTextureViewDimension_Cube;

      for (uint32_t bindingIndex = 0; bindingIndex < bindingCount;
           bindingIndex++) {
        if (bindings[bindingIndex].type != BindingType::eTextureCube ||
            bindings[bindingIndex].value.texture.slot != bindingDesc.index) {
          continue;
        }

        switch (sTextures[bindings[bindingIndex].value.texture.handle.idx]
                    .getFormat()) {
        case WGPUTextureFormat_Depth16Unorm:
        case WGPUTextureFormat_Depth24Plus:
        case WGPUTextureFormat_Depth24PlusStencil8:
        case WGPUTextureFormat_Depth32Float:
        case WGPUTextureFormat_Depth32FloatStencil8: {
          bindingEntries[i].texture.sampleType = WGPUTextureSampleType_Depth;
        } break;

        default: {
          bindingEntries[i].texture.sampleType = WGPUTextureSampleType_Float;
        } break;
        }

        break;
      }
    } break;

    case BindingType::eNone:
      sLogger->error("Unsupported binding type <{}> for {}",
                     (uint32_t)getBindings()[i].type);
      break;
    }
  }

  WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = {};
  bindGroupLayoutDesc.nextInChain = nullptr;
  // bindGroupLayoutDesc.label = path.c_str();
  bindGroupLayoutDesc.entryCount = static_cast<uint32_t>(bindingEntries.size());
  bindGroupLayoutDesc.entries = bindingEntries.data();

  return mBindGroupLayouts[hash] =
             wgpuDeviceCreateBindGroupLayout(sDevice, &bindGroupLayoutDesc);
}

void ShaderWebGPU::destroy() {
  wgpuShaderModuleRelease(mModule);
  mModule = NULL;
}

Result GraphicsProgramWebGPU::create(ShaderHandle sh, int flags,
                                     [[maybe_unused]] const std::string &name) {
  mShaderHandle = sh;
  mFlags = flags;

  return Result::eSuccess;
}

WGPURenderPipeline GraphicsProgramWebGPU::findOrCreatePipeline(
    const RenderTarget &target, WGPUBindGroupLayout bindGroupLayout,
    const VertexBufferHandle *vbhs, uint32_t vbCount) {
  uint32_t pipelineId;

  struct PipelineKey {
    int colorFlags[MAX_TARGET_COLOR_ATTACHMENTS];
    cbz::ImageHandle color[MAX_TARGET_COLOR_ATTACHMENTS];
    int depthFlags;
    cbz::ImageHandle depth;
  } key = {};

  for (size_t i = 0; i < target.colorAttachments.size(); i++) {
    key.color[i] = target.colorAttachments[i].imgh;
    key.colorFlags[i] = target.colorAttachments[i].flags;
  }

  if (target.depthAttachment.imgh.idx != CBZ_INVALID_HANDLE) {
    key.depth = target.depthAttachment.imgh;
    key.depthFlags = target.depthAttachment.flags;
  }

  MurmurHash3_x86_32(&key, sizeof(PipelineKey), 0, &pipelineId);

  if (auto it = mPipelines.find(pipelineId); it != mPipelines.end()) {
    return it->second;
  }

  const ShaderWebGPU *shader = &sShaders[mShaderHandle.idx];

  WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
  pipelineLayoutDesc.nextInChain = nullptr;
  pipelineLayoutDesc.label = nullptr;
  pipelineLayoutDesc.bindGroupLayoutCount = 1;
  pipelineLayoutDesc.bindGroupLayouts = &bindGroupLayout;

  WGPUPipelineLayout pipelineLayout = mPipelineLayouts[pipelineId] =
      wgpuDeviceCreatePipelineLayout(sDevice, &pipelineLayoutDesc);

  WGPURenderPipelineDescriptor pipelineDesc = {};
  pipelineDesc.nextInChain = nullptr;
  pipelineDesc.layout = pipelineLayout;

  WGPUVertexBufferLayout vbLayouts[MAX_VERTEX_INPUT_BINDINGS] = {};

  for (uint32_t vbIdx = 0; vbIdx < vbCount; vbIdx++) {
    const VertexBufferWebGPU *vb = &sVertexBuffers[vbhs[vbIdx].idx];
    vbLayouts[vbIdx].arrayStride = vb->getVertexLayout().stride;
    vbLayouts[vbIdx].stepMode =
        static_cast<WGPUVertexStepMode>(vb->getVertexLayout().stepMode);
    vbLayouts[vbIdx].attributeCount = vb->getVertexLayout().attributes.size();

    vbLayouts[vbIdx].attributes =
        (WGPUVertexAttribute const *)(vb->getVertexLayout().attributes.data());
  }

  WGPUVertexState vertexState = {};
  vertexState.nextInChain = nullptr;
  vertexState.module = shader->getModule();
  vertexState.entryPoint = "vertexMain";
  vertexState.constantCount = 0;
  vertexState.constants = nullptr;
  vertexState.bufferCount = vbCount;
  vertexState.buffers = vbLayouts;
  pipelineDesc.vertex = vertexState;

  WGPUPrimitiveState primitiveState = {};
  primitiveState.nextInChain = nullptr;
  primitiveState.topology = WGPUPrimitiveTopology_TriangleList;
  primitiveState.stripIndexFormat = WGPUIndexFormat_Undefined;

  primitiveState.frontFace = WGPUFrontFace_CCW;
  if ((mFlags & CBZ_GRAPHICS_PROGRAM_FRONT_FACE_CW) ==
      CBZ_GRAPHICS_PROGRAM_FRONT_FACE_CW) {
    primitiveState.frontFace = WGPUFrontFace_CW;
  }

  primitiveState.cullMode = WGPUCullMode_None;
  if ((mFlags & CBZ_GRAPHICS_PROGRAM_CULL_BACK) ==
      CBZ_GRAPHICS_PROGRAM_CULL_BACK) {
    primitiveState.cullMode = WGPUCullMode_Back;
  }

  if ((mFlags & CBZ_GRAPHICS_PROGRAM_CULL_FRONT) ==
      CBZ_GRAPHICS_PROGRAM_CULL_FRONT) {
    primitiveState.cullMode = WGPUCullMode_Front;
  }

  pipelineDesc.primitive = primitiveState;

  WGPUDepthStencilState depthStencilState = {};
  if (target.depthAttachment.imgh.idx != CBZ_INVALID_HANDLE) {
    const TextureWebGPU &depthTexture =
        sTextures[target.depthAttachment.imgh.idx];
    depthStencilState.nextInChain = nullptr;
    depthStencilState.format = depthTexture.getFormat();
    depthStencilState.depthWriteEnabled = true;

    if ((target.depthAttachment.flags &
         CBZ_RENDER_ATTACHMENT_DEPTH_WRITE_DISABLE) ==
        CBZ_RENDER_ATTACHMENT_DEPTH_WRITE_DISABLE) {
      depthStencilState.depthWriteEnabled = false;
    }

    depthStencilState.depthCompare = WGPUCompareFunction_LessEqual;

    depthStencilState.stencilReadMask = 0xFFFFFFFF;
    depthStencilState.stencilWriteMask = 0xFFFFFFFF;
    depthStencilState.depthBias = 0;
    depthStencilState.depthBiasSlopeScale = 0;
    depthStencilState.depthBiasClamp = 0;

    depthStencilState.stencilFront.compare = WGPUCompareFunction_Always;
    depthStencilState.stencilFront.failOp = WGPUStencilOperation_Keep;
    depthStencilState.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    depthStencilState.stencilFront.passOp = WGPUStencilOperation_Keep;

    depthStencilState.stencilBack.compare = WGPUCompareFunction_Always;
    depthStencilState.stencilBack.failOp = WGPUStencilOperation_Keep;
    depthStencilState.stencilBack.depthFailOp = WGPUStencilOperation_Keep;
    depthStencilState.stencilBack.passOp = WGPUStencilOperation_Keep;

    pipelineDesc.depthStencil = &depthStencilState;
  } else {
    pipelineDesc.depthStencil = nullptr;
  }

  WGPUMultisampleState multiSampleState = {};
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

  std::vector<WGPUColorTargetState> colorTargets(
      target.colorAttachments.size());

  for (size_t colorTargetIdx = 0; colorTargetIdx < colorTargets.size();
       colorTargetIdx++) {
    colorTargets[colorTargetIdx].nextInChain = nullptr;
    colorTargets[colorTargetIdx].format =
        sTextures[target.colorAttachments[colorTargetIdx].imgh.idx].getFormat();

    if ((target.colorAttachments[colorTargetIdx].flags &
         CBZ_RENDER_ATTACHMENT_BLEND) == CBZ_RENDER_ATTACHMENT_BLEND) {
      colorTargets[colorTargetIdx].blend = &blendState;
    } else {
      colorTargets[colorTargetIdx].blend = NULL;
    }

    colorTargets[colorTargetIdx].writeMask = WGPUColorWriteMask_All;
  }

  WGPUFragmentState fragmentState = {};
  fragmentState.nextInChain = nullptr;
  if ((shader->getShaderStages() & WGPUShaderStage_Fragment) ==
      WGPUShaderStage_Fragment) {
    fragmentState.entryPoint = "fragmentMain";
    fragmentState.constantCount = 0;
    fragmentState.constants = nullptr;

    fragmentState.targetCount = colorTargets.size();
    fragmentState.targets = colorTargets.data();
    fragmentState.module = shader->getModule();
    pipelineDesc.fragment = &fragmentState;
  } else {
    pipelineDesc.fragment = nullptr;
  }

  return mPipelines[pipelineId] =
             wgpuDeviceCreateRenderPipeline(sDevice, &pipelineDesc);
}

void GraphicsProgramWebGPU::destroy() {
  for (auto it : mPipelineLayouts) {
    wgpuPipelineLayoutRelease(it.second);
  }

  mPipelineLayouts.clear();

  for (auto it : mPipelines) {
    wgpuRenderPipelineRelease(it.second);
  }

  mPipelines.clear();
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
  // pipelineLayoutDesc.bindGroupLayouts = &shader->getBindGroupLayout();

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

Result RendererContextWebGPU::init(uint32_t width, uint32_t height, void *nwh,
                                   ImageHandle swapchainIMGH) {
  sLogger = spdlog::stdout_color_mt("cbzrenderer");
  sLogger->set_pattern("[%^%l%$] IRenderer: %v");

  mFrameCounter = 0;

  // net::Endpoint cbzEndPoint = {
  //     net::Address("192.168.1.4"),
  //     net::Port(6000),
  // };

  // sShaderHttpClient = net::httpClientCreate(cbzEndPoint);
  // if (!sShaderHttpClient) {
  //   sLogger->error("Failed to connect to shader compilation sever!)");
  //   return Result::eFailure;
  // }

  // sLogger->info("Shader compiler backend connected ({}:{})",
  // cbzEndPoint.address.c_str(), cbzEndPoint.port.c_str());

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
  // #ifdef WEBGPU_BACKEND_WGPU
  //   adaptorOpts.backendType = WGPUBackendType_Vulkan;
  // #endif
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

  sSurfaceFormat = WGPUTextureFormat_BGRA8UnormSrgb;
  wgpuAdapterRelease(adapter);

  int fbWidth, fbHeight;
  glfwGetFramebufferSize(static_cast<GLFWwindow *>(nwh), &fbWidth, &fbHeight);

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
  surfaceConfig.width = fbWidth;
  surfaceConfig.height = fbHeight;
  surfaceConfig.presentMode = WGPUPresentMode_Fifo;
  wgpuSurfaceConfigure(sSurface, &surfaceConfig);

  // Reserve for current swapchain image
  sTextures.resize(swapchainIMGH.idx + 1u);
  sSurfaceIMGH = swapchainIMGH;

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiStyle &style = ImGui::GetStyle();
  style.ScaleAllSizes(2.0f);

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOther(static_cast<GLFWwindow *>(nwh), true);
  ImGui_ImplWGPU_Init(sDevice, 3, sSurfaceFormat);

  sLogger->info("Cubozoa initialized!");
  return Result::eSuccess;
}

uint32_t RendererContextWebGPU::submitSorted(
    const std::vector<RenderTarget> &renderTargets,
    const ShaderProgramCommand *sortedCmds, uint32_t count) {

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
    return mFrameCounter;
  }

  // Re assign surface texture handle
  // TODO: Fix. findOrCreateTextureView is cached by image id, but each surface
  // texture shares the same id. Most likely have to Create abstraction for
  // swapchain.
  sTextures[sSurfaceIMGH.idx] = {};
  sTextures[sSurfaceIMGH.idx].create(surfaceTexture.texture);
  WGPUTextureView swapchainTextureView =
      sTextures[sSurfaceIMGH.idx].findOrCreateTextureView(
          WGPUTextureAspect_All);

  WGPUCommandEncoderDescriptor cmdEncoderDesc = {};
  cmdEncoderDesc.nextInChain = nullptr;
  cmdEncoderDesc.label = "CommandEncoderFrameX";

  WGPUCommandEncoder cmdEncoder =
      wgpuDeviceCreateCommandEncoder(sDevice, &cmdEncoderDesc);

  // Target struct
  uint8_t target = CBZ_INVALID_RENDER_TARGET;
  CBZTargetType targetType = CBZ_TARGET_TYPE_NONE;
  uint64_t targetSortKey = std::numeric_limits<uint64_t>::max();

  // Compute state
  uint32_t dispatchX = 0;
  uint32_t dispatchY = 0;
  uint32_t dispatchZ = 0;
  WGPUComputePassEncoder computePassEncoder = nullptr;

  // Graphics state
  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;
  bool isIndexed = false;
  WGPURenderPassEncoder renderPassEncoder = nullptr;

  for (uint32_t cmdIdx = 0; cmdIdx < count; cmdIdx++) {
    const ShaderProgramCommand &renderCmd = sortedCmds[cmdIdx];

    // Switch targets
    if (target != renderCmd.target) {

      // End previous pass
      switch (targetType) {
      case CBZ_TARGET_TYPE_COMPUTE: {
        if (computePassEncoder != NULL) {
          wgpuComputePassEncoderEnd(computePassEncoder);
          wgpuComputePassEncoderRelease(computePassEncoder);

          // Clear sort key; Targets may use the same program back to back.
          // This forces pipelines to rebind each target switch.
          targetSortKey = std::numeric_limits<uint32_t>::max();
        }
      } break;

      case CBZ_TARGET_TYPE_GRAPHICS: {
        if (renderPassEncoder != NULL) {
          wgpuRenderPassEncoderEnd(renderPassEncoder);
          wgpuRenderPassEncoderRelease(renderPassEncoder);

          // Clear sort key; Targets may use the same program back to back.
          // This forces pipelines to rebind each target switch.
          targetSortKey = std::numeric_limits<uint32_t>::max();
        }
      } break;

      case CBZ_TARGET_TYPE_NONE: {
      } break;
      }

      // Assign new current target
      target = renderCmd.target;
      targetType = renderCmd.programType;

      // Begin pass
      switch (renderCmd.programType) {
      case CBZ_TARGET_TYPE_COMPUTE: {
        WGPUComputePassDescriptor computePassDesc = {};
        computePassDesc.nextInChain = nullptr;

        static std::string computePassLabel;
        computePassLabel = "ComputePass" + std::to_string(renderCmd.target);
        computePassDesc.label = computePassLabel.c_str();
        computePassDesc.timestampWrites = nullptr;

        computePassEncoder =
            wgpuCommandEncoderBeginComputePass(cmdEncoder, &computePassDesc);
      } break;

      case CBZ_TARGET_TYPE_GRAPHICS: {
        if (renderCmd.target != CBZ_DEFAULT_RENDER_TARGET) {
          const RenderTarget &renderTarget = renderTargets[renderCmd.target];

          static std::array<WGPURenderPassColorAttachment,
                            MAX_TARGET_COLOR_ATTACHMENTS>
              colorAttachments;

          for (size_t colorAttachmentIdx = 0;
               colorAttachmentIdx <
               renderTargets[renderCmd.target].colorAttachments.size();
               colorAttachmentIdx++) {

            colorAttachments[colorAttachmentIdx].nextInChain = nullptr;
            colorAttachments[colorAttachmentIdx].view =
                sTextures[renderTarget.colorAttachments[colorAttachmentIdx]
                              .imgh.idx]
                    .findOrCreateTextureView(
                        WGPUTextureAspect_All,
                        renderTarget.colorAttachments[colorAttachmentIdx]
                            .baseArrayLayer,
                        renderTarget.colorAttachments[colorAttachmentIdx]
                            .arrayLayerCount);

            colorAttachments[colorAttachmentIdx].loadOp = WGPULoadOp_Clear;

            if ((renderTarget.colorAttachments[colorAttachmentIdx].flags) &
                CBZ_RENDER_ATTACHMENT_LOAD) {
              colorAttachments[colorAttachmentIdx].loadOp = WGPULoadOp_Load;
            }

            colorAttachments[colorAttachmentIdx].storeOp = WGPUStoreOp_Store;
            colorAttachments[colorAttachmentIdx].clearValue = {0.0f, 0.0f, 0.0f,
                                                               1.0f};
          }

          WGPURenderPassDepthStencilAttachment depthStencilAttachment = {};
          if (renderTarget.depthAttachment.imgh.idx != CBZ_INVALID_HANDLE) {
            depthStencilAttachment.view =
                sTextures[renderTarget.depthAttachment.imgh.idx]
                    .findOrCreateTextureView(WGPUTextureAspect_DepthOnly);

            depthStencilAttachment.depthLoadOp = WGPULoadOp_Clear;
            if ((renderTarget.depthAttachment.flags) &
                CBZ_RENDER_ATTACHMENT_LOAD) {
              depthStencilAttachment.depthLoadOp = WGPULoadOp_Load;
            }

            depthStencilAttachment.depthStoreOp = WGPUStoreOp_Store;
            depthStencilAttachment.depthClearValue = 1.0f;
            depthStencilAttachment.depthReadOnly = false;

            depthStencilAttachment.stencilClearValue = 0;
            depthStencilAttachment.stencilLoadOp = WGPULoadOp_Clear;
            depthStencilAttachment.stencilStoreOp = WGPUStoreOp_Store;
            depthStencilAttachment.stencilReadOnly = true;
          }

#ifndef WEBGPU_BACKEND_WGPU
          renderPassColorAttachmentDesc.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif // NOT WEBGPU_BACKEND_WGPU

          WGPURenderPassDescriptor renderPassDesc = {};
          renderPassDesc.nextInChain = nullptr;

          static std::string renderPassLabel;
          renderPassLabel = "RenderPass" + std::to_string(renderCmd.target);
          renderPassDesc.label = renderPassLabel.c_str();
          renderPassDesc.colorAttachmentCount =
              renderTarget.colorAttachments.size();
          renderPassDesc.colorAttachments = colorAttachments.data();
          renderPassDesc.depthStencilAttachment =
              renderTarget.depthAttachment.imgh.idx != CBZ_INVALID_HANDLE
                  ? &depthStencilAttachment
                  : nullptr;
          renderPassDesc.occlusionQuerySet = nullptr;
          renderPassDesc.timestampWrites = nullptr;

          renderPassEncoder =
              wgpuCommandEncoderBeginRenderPass(cmdEncoder, &renderPassDesc);
        } else { // Render to swapchain; target is 'CBZ_DEFAULT_RENDER_TARGET'
          WGPURenderPassColorAttachment renderPassColorAttachmentDesc = {};
          renderPassColorAttachmentDesc.nextInChain = nullptr;
          renderPassColorAttachmentDesc.view = swapchainTextureView;
          // renderPassColorAttachmentDesc.resolveTarget ;
          renderPassColorAttachmentDesc.loadOp = WGPULoadOp_Clear;
          renderPassColorAttachmentDesc.storeOp = WGPUStoreOp_Store;
          renderPassColorAttachmentDesc.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};

#ifndef WEBGPU_BACKEND_WGPU
          renderPassColorAttachmentDesc.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif // NOT WEBGPU_BACKEND_WGPU

          WGPURenderPassDescriptor renderPassDesc = {};
          renderPassDesc.nextInChain = nullptr;
          renderPassDesc.label = "SwapchainRenderpass";
          renderPassDesc.colorAttachmentCount = 1;
          renderPassDesc.colorAttachments = &renderPassColorAttachmentDesc;
          renderPassDesc.depthStencilAttachment = nullptr;
          renderPassDesc.occlusionQuerySet = nullptr;
          renderPassDesc.timestampWrites = nullptr;

          renderPassEncoder =
              wgpuCommandEncoderBeginRenderPass(cmdEncoder, &renderPassDesc);
        }
      }

      case CBZ_TARGET_TYPE_NONE: {
      } break;
      }
    }

    // Execute cmds
    switch (targetType) {
    case CBZ_TARGET_TYPE_COMPUTE: {
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
    } break;

    case CBZ_TARGET_TYPE_GRAPHICS: {
      if (targetSortKey != renderCmd.sortKey) {
        targetSortKey = renderCmd.sortKey;

        GraphicsProgramWebGPU &graphicsProgram =
            sGraphicsPrograms[renderCmd.program.graphics.ph.idx];

        // const VertexBufferWebGPU &vb =
        //     sVertexBuffers[renderCmd.program.graphics.vbh.idx];
        //
        // if (sShaders[graphicsProgram.getShader().idx].getVertexLayout() !=
        //     vb.getVertexLayout()) {
        //   // TODO: Handle multiple vertexBuffers
        //   sLogger->warn(
        //       "Incompatible vertex buffer and program layout for '{}'",
        //       HandleProvider<GraphicsProgramHandle>::getName(
        //           renderCmd.program.graphics.ph));
        //   sLogger->warn("Discarding draw...");
        //   continue;
        // }
        //

        WGPUBindGroupLayout bindGroupLayout =
            sShaders[graphicsProgram.getShader().idx]
                .findOrCreateBindGroupLayout(
                    renderCmd.bindings.data(),
                    static_cast<uint32_t>(renderCmd.bindings.size()));

        WGPURenderPipeline renderPipeline = nullptr;
        if (renderCmd.target != CBZ_DEFAULT_RENDER_TARGET) {
          renderPipeline = graphicsProgram.findOrCreatePipeline(
              renderTargets[renderCmd.target], bindGroupLayout,
              renderCmd.program.graphics.vbhs,
              renderCmd.program.graphics.vbCount);
        } else {
          // Surface render target
          static RenderTarget sSurfaceRenderTarget{};
          sSurfaceRenderTarget.colorAttachments.resize(1);
          sSurfaceRenderTarget.colorAttachments[0].imgh = sSurfaceIMGH;

          renderPipeline = graphicsProgram.findOrCreatePipeline(
              sSurfaceRenderTarget, bindGroupLayout,
              renderCmd.program.graphics.vbhs,
              renderCmd.program.graphics.vbCount);
        }

        if (!renderPipeline) {
          sLogger->error("Failed to create render pipeline !");
          sLogger->error("Discarding draw...");
          continue;
        }

        wgpuRenderPassEncoderSetPipeline(renderPassEncoder, renderPipeline);

        const WGPUBindGroup graphicsBindGroup = findOrCreateBindGroup(
            graphicsProgram.getShader(), renderCmd.getDescriptorHash(),
            renderCmd.bindings.data(),
            static_cast<uint32_t>(renderCmd.bindings.size()));

        if (graphicsBindGroup) {
          uint32_t offsets = 0;
          wgpuRenderPassEncoderSetBindGroup(renderPassEncoder, 0,
                                            graphicsBindGroup, 0, &offsets);
        } else {
          sLogger->error("Failed to create bind group for {}!",
                         HandleProvider<GraphicsProgramHandle>::getName(
                             renderCmd.program.graphics.ph));
        }

        // Bind all vertex buffers
        for (uint32_t vbIdx = 0; vbIdx < renderCmd.program.graphics.vbCount;
             vbIdx++) {
          if (sVertexBuffers[renderCmd.program.graphics.vbhs[vbIdx].idx].bind(
                  renderPassEncoder, vbIdx) != Result::eSuccess) {
            spdlog::error("Failed to bind vertex buffer!");
          };
        }

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
      };

      if (isIndexed) {
        if (renderCmd.program.graphics.instances > 1) {
          wgpuRenderPassEncoderDrawIndexed(renderPassEncoder, indexCount,
                                           renderCmd.program.graphics.instances,
                                           0, 0, 0);
        } else {
          wgpuRenderPassEncoderDrawIndexed(renderPassEncoder, indexCount,
                                           renderCmd.program.graphics.instances,
                                           0, 0, renderCmd.submissionID);
        }
      } else {
        wgpuRenderPassEncoderDraw(renderPassEncoder, vertexCount, 1, 0,
                                  renderCmd.submissionID);
        spdlog::error("Non indexed drawing unsupported!");
      }
    } break;

    case CBZ_TARGET_TYPE_NONE: {
      sLogger->critical("Uknown render target!");
    } break;
    }
  }

  // End trailing pass
  switch (targetType) {
  case CBZ_TARGET_TYPE_GRAPHICS: {
    if (renderPassEncoder != NULL) {
      if (target == CBZ_DEFAULT_RENDER_TARGET) {
        ImGui_ImplWGPU_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // User defined imgui render
        if (sImguiRenderfunc) {
          sImguiRenderfunc();
        }

        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), renderPassEncoder);
      }

      wgpuRenderPassEncoderEnd(renderPassEncoder);
      wgpuRenderPassEncoderRelease(renderPassEncoder);
      renderPassEncoder = nullptr;
    }
  } break;

  case CBZ_TARGET_TYPE_COMPUTE: {
    if (computePassEncoder != NULL) {
      wgpuComputePassEncoderEnd(computePassEncoder);
      wgpuComputePassEncoderRelease(computePassEncoder);
      computePassEncoder = nullptr;
    }
  } break;

  case CBZ_TARGET_TYPE_NONE:
    break;
  }

  WGPUCommandBufferDescriptor cmdDesc = {};
  cmdDesc.nextInChain = nullptr;
  std::string commandBufferLabel =
      "CommandBuffer" + std::to_string(mFrameCounter);
  cmdDesc.label = commandBufferLabel.c_str();

  WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(cmdEncoder, &cmdDesc);
  wgpuCommandEncoderRelease(cmdEncoder);

  wgpuQueueSubmit(sQueue, 1, &cmd);
  wgpuCommandBufferRelease(cmd);

#ifndef __EMSCRIPTEN__
  wgpuSurfacePresent(sSurface);
#endif

  wgpuTextureRelease(surfaceTexture.texture);
  wgpuTextureViewRelease(swapchainTextureView);

  PollEvents(false);
  return mFrameCounter++;
}

Result
RendererContextWebGPU::vertexBufferCreate(VertexBufferHandle vbh,
                                          const VertexLayout &vertexLayout,
                                          uint32_t count, const void *data) {
  if (sVertexBuffers.size() < vbh.idx + 1u) {
    sVertexBuffers.resize(vbh.idx + 1u);
  }

  return sVertexBuffers[vbh.idx].create(vertexLayout, count, data);
}

void RendererContextWebGPU::vertexBufferDestroy(VertexBufferHandle vbh) {
  return sVertexBuffers[vbh.idx].destroy();
}

Result RendererContextWebGPU::indexBufferCreate(IndexBufferHandle ibh,
                                                CBZIndexFormat format,
                                                uint32_t count,
                                                const void *data) {
  if (sIndexBuffers.size() < ibh.idx + 1u) {
    sIndexBuffers.resize(ibh.idx + 1u);
  }

  return sIndexBuffers[ibh.idx].create(static_cast<WGPUIndexFormat>(format),
                                       count, data);
}

void RendererContextWebGPU::indexBufferDestroy(IndexBufferHandle ibh) {
  return sIndexBuffers[ibh.idx].destroy();
}

Result RendererContextWebGPU::uniformBufferCreate(UniformHandle uh,
                                                  CBZUniformType type,
                                                  uint16_t num,
                                                  const void *data) {
  if (sUniformBuffers.size() < uh.idx + 1u) {
    sUniformBuffers.resize(uh.idx + 1u);
  }

  return sUniformBuffers[uh.idx].create(
      type, num, data, HandleProvider<UniformHandle>::getName(uh));
}

void RendererContextWebGPU::uniformBufferUpdate(UniformHandle uh,
                                                const void *data,
                                                uint16_t num) {
  sUniformBuffers[uh.idx].update(data, num);
}

void RendererContextWebGPU::uniformBufferDestroy(UniformHandle uh) {
  return sUniformBuffers[uh.idx].destroy();
}

Result RendererContextWebGPU::structuredBufferCreate(StructuredBufferHandle sbh,
                                                     CBZUniformType type,
                                                     uint32_t elementCount,
                                                     const void *elementData,
                                                     int flags) {
  if (sStorageBuffers.size() < static_cast<uint64_t>(sbh.idx + 1u)) {
    sStorageBuffers.resize(static_cast<uint64_t>(sbh.idx + 1u));
  }

  WGPUBufferUsageFlags usageFlags = 0;

  if ((CBZ_BUFFER_COPY_SRC & flags) == CBZ_BUFFER_COPY_SRC) {
    usageFlags |= WGPUBufferUsage_CopySrc;
  }

  if ((CBZ_BUFFER_COPY_DST & flags) == CBZ_BUFFER_COPY_DST) {
    usageFlags |= WGPUBufferUsage_CopyDst;
  }

  return sStorageBuffers[sbh.idx].create(
      type, elementCount, elementData, usageFlags,
      HandleProvider<StructuredBufferHandle>::getName(sbh));
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

Result RendererContextWebGPU::imageCreate(ImageHandle th,
                                          CBZTextureFormat format, uint32_t w,
                                          uint32_t h, uint32_t depth,
                                          CBZTextureDimension dimension,
                                          CBZImageFlags flags) {
  if (sTextures.size() < th.idx + 1u) {
    sTextures.resize(th.idx + 1u);
  }

  WGPUTextureUsageFlags wgpuUsageFlags = 0;

  if ((flags & CBZ_IMAGE_RENDER_ATTACHMENT) == CBZ_IMAGE_RENDER_ATTACHMENT) {
    wgpuUsageFlags |= WGPUTextureUsage_RenderAttachment;
  }

  if ((flags & CBZ_IMAGE_BINDING) == CBZ_IMAGE_BINDING) {
    wgpuUsageFlags |= WGPUTextureUsage_TextureBinding;
  }

  if ((flags & CBZ_IMAGE_COPY_SRC) == CBZ_IMAGE_COPY_SRC) {
    wgpuUsageFlags |= WGPUTextureUsage_CopySrc;
  }

  return sTextures[th.idx].create(w, h, depth, TextureDimToWGPU(dimension),
                                  static_cast<WGPUTextureFormat>(format),
                                  wgpuUsageFlags);
}

void RendererContextWebGPU::imageUpdate(ImageHandle th, void *data,
                                        uint32_t count) {
  sTextures[th.idx].update(data, count);
};

void RendererContextWebGPU::imageDestroy(ImageHandle th) {
  return sTextures[th.idx].destroy();
};

Result RendererContextWebGPU::shaderCreate(ShaderHandle sh,
                                           CBZShaderFlags flags,
                                           const std::string &path) {
  if (sShaders.size() < sh.idx + 1u) {
    sShaders.resize(sh.idx + 1u);
  }

  return sShaders[sh.idx].create(path, flags);
}

void RendererContextWebGPU::shaderDestroy(ShaderHandle sh) {
  return sShaders[sh.idx].destroy();
}

Result RendererContextWebGPU::graphicsProgramCreate(GraphicsProgramHandle gph,
                                                    ShaderHandle sh,
                                                    int flags) {
  if (sGraphicsPrograms.size() < gph.idx + 1u) {
    sGraphicsPrograms.resize(gph.idx + 1u);
  }

  return sGraphicsPrograms[gph.idx].create(
      sh, flags, HandleProvider<GraphicsProgramHandle>::getName(gph));
}

void RendererContextWebGPU::graphicsProgramDestroy(GraphicsProgramHandle gph) {
  return sGraphicsPrograms[gph.idx].destroy();
}

Result RendererContextWebGPU::computeProgramCreate(ComputeProgramHandle cph,
                                                   ShaderHandle sh) {
  if (sComputePrograms.size() < cph.idx + 1u) {
    sComputePrograms.resize(cph.idx + 1u);
  }

  return sComputePrograms[cph.idx].create(sh);
}

void RendererContextWebGPU::computeProgramDestroy(ComputeProgramHandle cph) {
  return sComputePrograms[cph.idx].destroy();
}

void RendererContextWebGPU::shutdown() {
  if (mStagingBuffer) {
    wgpuBufferDestroy(mStagingBuffer);
  }

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

  const std::vector<BindingDesc> &shaderBindingDescs =
      sShaders[sh.idx].getBindings();

  if (sBindingGroups.find(descriptorHash) == sBindingGroups.end()) {

    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.nextInChain = nullptr;
    bindGroupDesc.label = nullptr;
    bindGroupDesc.layout =
        sShaders[sh.idx].findOrCreateBindGroupLayout(bindings, bindingCount);

    std::vector<WGPUBindGroupEntry> bindGroupEntries;
    for (const BindingDesc &bindingDesc : shaderBindingDescs) {
      switch (bindingDesc.type) {
      case BindingType::eUniformBuffer: {
        const Binding *binding = nullptr;

        // Find uniform by name
        for (uint32_t inputBindingIdx = 0; inputBindingIdx < bindingCount;
             inputBindingIdx++) {

          if (bindings[inputBindingIdx].type != BindingType::eUniformBuffer) {
            continue;
          }

          if (bindingDesc.name ==
              HandleProvider<UniformHandle>::getName(
                  bindings[inputBindingIdx].value.uniformBuffer.handle)) {
            binding = &bindings[inputBindingIdx];
            break;
          }
        }

        if (!binding) {
          sLogger->error(
              "Shader program '{}' has no uniform binding named '{}'",
              HandleProvider<ShaderHandle>::getName(sh), bindingDesc.name);
          return nullptr;
        }

        bindGroupEntries.push_back(
            sUniformBuffers[binding->value.uniformBuffer.handle.idx]
                .createBindGroupEntry(bindingDesc.index));
      } break;

      case BindingType::eRWStructuredBuffer:
      case BindingType::eStructuredBuffer: {
        const Binding *binding = nullptr;

        // Find buffer binding by index/slot
        for (uint32_t inputBindingIdx = 0; inputBindingIdx < bindingCount;
             inputBindingIdx++) {

          if (bindings[inputBindingIdx].type !=
                  BindingType::eRWStructuredBuffer &&
              bindings[inputBindingIdx].type !=
                  BindingType::eStructuredBuffer) {
            continue;
          }

          if (bindingDesc.index ==
              bindings[inputBindingIdx].value.storageBuffer.slot) {
            binding = &bindings[inputBindingIdx];
            break;
          }
        }

        if (!binding) {
          sLogger->error("Shader program '{}' has no buffer binding at {}",
                         HandleProvider<ShaderHandle>::getName(sh),
                         bindingDesc.index);
          return nullptr;
        }

        StructuredBufferHandle sbh = binding->value.storageBuffer.handle;
        bindGroupEntries.push_back(
            sStorageBuffers[sbh.idx].createBindGroupEntry(bindingDesc.index));
      } break;

      case BindingType::eTexture2D: {
        const Binding *binding = nullptr;

        // Find texture binding by index/slot
        for (uint32_t inputBindingIdx = 0; inputBindingIdx < bindingCount;
             inputBindingIdx++) {

          if (bindings[inputBindingIdx].type != BindingType::eTexture2D) {
            continue;
          }

          if (bindingDesc.index ==
              bindings[inputBindingIdx].value.storageBuffer.slot) {
            binding = &bindings[inputBindingIdx];
            break;
          }
        }

        if (!binding) {
          sLogger->error("Shader program '{}' has no texture binding at {}",
                         HandleProvider<ShaderHandle>::getName(sh),
                         bindingDesc.index);
          return nullptr;
        }

        ImageHandle th = binding->value.texture.handle;

        WGPUBindGroupEntry &entry = bindGroupEntries.emplace_back();
        entry.nextInChain = nullptr;
        entry.binding = bindingDesc.index;
        entry.offset = 0;
        entry.textureView = sTextures[th.idx].findOrCreateTextureView(
            WGPUTextureAspect_All, 0, 1, CBZ_TEXTURE_VIEW_DIMENSION_2D);
      } break;

      case BindingType::eTextureCube: {
        const Binding *binding = nullptr;

        // Find texture binding by index/slot
        for (uint32_t inputBindingIdx = 0; inputBindingIdx < bindingCount;
             inputBindingIdx++) {

          if (bindings[inputBindingIdx].type != BindingType::eTextureCube) {
            continue;
          }

          if (bindingDesc.index ==
              bindings[inputBindingIdx].value.storageBuffer.slot) {
            binding = &bindings[inputBindingIdx];
            break;
          }
        }

        if (!binding) {
          sLogger->error(
              "Shader program '{}' has no texture cube binding at {}",
              HandleProvider<ShaderHandle>::getName(sh), bindingDesc.index);
          return nullptr;
        }

        ImageHandle th = binding->value.texture.handle;

        WGPUBindGroupEntry &entry = bindGroupEntries.emplace_back();
        entry.nextInChain = nullptr;
        entry.binding = bindingDesc.index;
        entry.offset = 0;
        entry.textureView = sTextures[th.idx].findOrCreateTextureView(
            WGPUTextureAspect_All, 0, 6, CBZ_TEXTURE_VIEW_DIMENSION_CUBE);
      } break;

      case BindingType::eSampler: {
        const Binding *binding = nullptr;

        // Find texture binding by index/slot
        for (uint32_t inputBindingIdx = 0; inputBindingIdx < bindingCount;
             inputBindingIdx++) {

          if (bindings[inputBindingIdx].type != BindingType::eSampler) {
            continue;
          }

          if (bindingDesc.index ==
              bindings[inputBindingIdx].value.storageBuffer.slot) {
            binding = &bindings[inputBindingIdx];
            break;
          }
        }

        if (!binding) {
          sLogger->error(
              "Shader program '{}' has no sampler cube binding at {}",
              HandleProvider<ShaderHandle>::getName(sh), bindingDesc.index);
          return nullptr;
        }

        SamplerHandle samplerHandle = binding->value.sampler.handle;

        const auto &it = std::find_if(
            shaderBindingDescs.begin(), shaderBindingDescs.end(),
            [=](const BindingDesc &bindingDesc) {
              return bindingDesc.index == binding->value.sampler.slot;
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

        WGPUBindGroupEntry &entry = bindGroupEntries.emplace_back();
        entry.binding = it->index;
        entry.nextInChain = nullptr;
        entry.sampler = sSamplers[samplerHandle.idx];
      } break;

      case BindingType::eNone: {
        sLogger->error("Uknown and unsupported binding!");
      } break;
      }
    }

    bindGroupDesc.entryCount = shaderBindingDescs.size();
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
