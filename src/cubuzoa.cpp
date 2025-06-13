#include "cubozoa/cubozoa.h"
#include "spdlog/spdlog.h"

#include <GLFW/glfw3.h>
#include <cstdint>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef WEBGPU_BACKEND_WGPU
#include <webgpu/wgpu.h>
#endif

#include <glfw3webgpu.h>
#include <webgpu/webgpu.h>

#include "core/cbz_file.h"

static std::shared_ptr<spdlog::logger> sLogger;

static GLFWwindow *sWindow;
static WGPUDevice sDevice;
static WGPUQueue sQueue;
static WGPUSurface sSurface;
static WGPUTextureFormat sSurfaceFormat;

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

class ShaderWGPU {
public:
  [[nodiscard]] Result create(const std::string &code,
                              const std::string &name = "");
  void destroy();

  [[nodiscard]] inline WGPUShaderModule asModule() const { return mModule; };

private:
  WGPUShaderModule mModule;
};

Result ShaderWGPU::create(const std::string &code, const std::string &name) {
  WGPUShaderModuleDescriptor shaderModuleDesc{};

  WGPUShaderModuleWGSLDescriptor shaderCodeDesc;
  shaderCodeDesc.chain.next = nullptr;
  shaderCodeDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
  shaderCodeDesc.code = code.c_str();

  shaderModuleDesc.nextInChain = &shaderCodeDesc.chain;
  shaderModuleDesc.label = name.c_str();
#ifdef WEBGPU_BACKEND_WGPU
  shaderModuleDesc.hintCount = 0;
  shaderModuleDesc.hints = nullptr;
#endif
  mModule = wgpuDeviceCreateShaderModule(sDevice, &shaderModuleDesc);

  return Result::Success;
}

void ShaderWGPU::destroy() { wgpuShaderModuleRelease(mModule); }

class ProgramWGPU {
public:
  [[nodiscard]] Result create(ShaderWGPU *vs, ShaderWGPU *fs = nullptr,
                              const std::string &name = "");

  [[nodiscard]] Result bind(WGPURenderPassEncoder renderPassEncoder) const;

  void destroy();

private:
  WGPURenderPipeline mPipeline;
};

Result ProgramWGPU::create(ShaderWGPU *vs, ShaderWGPU *fs,
                           const std::string &name) {
  if (!vs) {
    spdlog::error("Graphics pipeline must have valid vertex shader!");
    return Result::WGPUError;
  }

  WGPURenderPipelineDescriptor pipelineDesc = {};
  pipelineDesc.nextInChain = nullptr;
  pipelineDesc.label = name.c_str();

  // std::string layoutName = std::string(name) + std::string("_layout");
  // WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
  // pipelineLayoutDesc.nextInChain = nullptr;
  // pipelineLayoutDesc.label = layoutName.c_str();
  // // pipelineLayoutDesc.bindGroupLayoutCount;
  // // pipelineLayoutDesc.bindGroupLayouts;
  // WGPUPipelineLayout pipelineLayout =
  //     wgpuDeviceCreatePipelineLayout(sDevice, &pipelineLayoutDesc);
  // pipelineDesc.layout = pipelineLayout;
  pipelineDesc.layout = nullptr;

  WGPUVertexState vertexState = {};
  vertexState.nextInChain = nullptr;
  vertexState.module = vs->asModule();
  vertexState.entryPoint = "vsMain";
  vertexState.constantCount = 0;
  vertexState.constants = nullptr;
  vertexState.bufferCount = 0;
  vertexState.buffers = nullptr;
  pipelineDesc.vertex = vertexState;

  WGPUPrimitiveState primitiveState = {};
  primitiveState.nextInChain = nullptr;
  primitiveState.topology = WGPUPrimitiveTopology_TriangleList;
  primitiveState.stripIndexFormat = WGPUIndexFormat_Undefined;
  primitiveState.frontFace = WGPUFrontFace_CCW;
  primitiveState.cullMode = WGPUCullMode_Back;
  pipelineDesc.primitive = primitiveState;

  pipelineDesc.depthStencil = nullptr;

  WGPUMultisampleState multiSampleState;
  multiSampleState.nextInChain = nullptr;
  multiSampleState.count = 1;
  multiSampleState.mask = ~0u;
  multiSampleState.alphaToCoverageEnabled = false;
  pipelineDesc.multisample = multiSampleState;

  // rgb = srcFactor * srcRgb [operation] dstFactor * dstRgb
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
  if (fs) {
    fragmentState.entryPoint = "fsMain";
    fragmentState.constantCount = 0;
    fragmentState.constants = nullptr;

    fragmentState.targetCount = 1;
    fragmentState.targets = &targetState;
    fragmentState.module = fs->asModule();
    pipelineDesc.fragment = &fragmentState;
  } else {
    pipelineDesc.fragment = nullptr;
  }

  mPipeline = wgpuDeviceCreateRenderPipeline(sDevice, &pipelineDesc);
  return Result::Success;
}

Result ProgramWGPU::bind(WGPURenderPassEncoder renderPassEncoder) const {
  wgpuRenderPassEncoderSetPipeline(renderPassEncoder, mPipeline);
  return Result::Success;
}

// TODO: Remove
ShaderWGPU triangleShader;
ProgramWGPU graphicsProgram;

Result App::init(InitDesc initDesc) {
  sLogger = spdlog::stdout_color_mt("wgpu");
  sLogger->set_level(spdlog::level::trace);
  sLogger->set_pattern("[%^%l%$][CBZ] %v");

  if (glfwInit() != GLFW_TRUE) {
    spdlog::error("Failed to initialize glfw!");
    return Result::GLFWError;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  sWindow = glfwCreateWindow(initDesc.width, initDesc.height, initDesc.name,
                             NULL, NULL);

  if (!sWindow) {
    spdlog::error("Failed to create window!");
    glfwTerminate();
    return Result::GLFWError;
  }

#ifdef WEBGPU_BACKEND_EMISCRIPTEN
  WGPUInstance instance = wgpuCreateInstance(nullptr);
#else
  WGPUInstanceDescriptor instanceDesc = {};
  WGPUInstance instance = wgpuCreateInstance(&instanceDesc);
#endif

  sSurface = glfwGetWGPUSurface(instance, sWindow);

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
          request->result = Result::Success;
          break;
        case WGPURequestAdapterStatus_Unavailable:
        case WGPURequestAdapterStatus_Error:
        case WGPURequestAdapterStatus_Unknown:
        default:
          spdlog::error("{}", message);
          request->result = Result::WGPUError;
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
  if (adapterRequest.result != Result::Success) {
    sLogger->error("Failed to request adapter!");
    return Result::Failure;
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
#endif

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

  WGPUDeviceDescriptor deviceDesc = {};
  deviceDesc.nextInChain = nullptr;
  deviceDesc.label = "WGPUDevice";
  deviceDesc.requiredFeatures = nullptr;
  deviceDesc.requiredLimits = nullptr;
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
          request->result = Result::Success;
          break;
        case WGPURequestDeviceStatus_Error:
        case WGPURequestDeviceStatus_Unknown:
        case WGPURequestDeviceStatus_Force32:
          request->result = Result::WGPUError;
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
  if (deviceRequest.result != Result::Success) {
    return Result::WGPUError;
  }

  sDevice = deviceRequest.device;

  wgpuDeviceSetUncapturedErrorCallback(sDevice, UncapturedErrorCallback,
                                       nullptr);
  sQueue = wgpuDeviceGetQueue(sDevice);
  wgpuQueueOnSubmittedWorkDone(sQueue, OnWorkDone, nullptr);

  sSurfaceFormat = wgpuSurfaceGetPreferredFormat(sSurface, adapter);
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
  surfaceConfig.width = initDesc.width;
  surfaceConfig.height = initDesc.height;
  surfaceConfig.presentMode = WGPUPresentMode_Fifo;
  wgpuSurfaceConfigure(sSurface, &surfaceConfig);

  std::vector<uint8_t> triangleCode;
  if (loadFileFromPath(
          "/Users/christianmarkg.solon/dev/cubozoa/examples/assets/"
          "shaders/triangle.wgsl",
          triangleCode) != Result::Success) {
    exit(-1);
  }

  // TODO: Remove
  if (triangleShader.create((char *)(triangleCode.data())) != Result::Success) {
    exit(-1);
  };

  if (graphicsProgram.create(&triangleShader, &triangleShader) !=
      Result::Success) {
    exit(-1);
  };

  sLogger->info("Cubozoa initialized!");
  return Result::Success;
}

void App::run() {
  bool isRunning = true;
  while (!glfwWindowShouldClose(sWindow) && isRunning) {
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
      spdlog::error("Failed to get surface texture!");
      break;
    }

    WGPUTextureViewDescriptor surfaceTextureViewDesc{};
    surfaceTextureViewDesc.nextInChain = NULL;
    surfaceTextureViewDesc.label = "surfaceTextureViewX";
    surfaceTextureViewDesc.format =
        wgpuTextureGetFormat(surfaceTexture.texture);
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
    renderPassColorAttachmentDesc.clearValue = {1.0f, 1.0f, 0.0f, 1.0f};

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

    for (int sortedPrograms = 0; sortedPrograms < 1; sortedPrograms++) {
      if (graphicsProgram.bind(renderPassEncoder) != Result::Success) {
        continue;
      };

      wgpuRenderPassEncoderDraw(renderPassEncoder, 3, 1, 0, 0);
    }

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

#ifdef WEBGPU_BACKEND_WGPU
    wgpuTextureViewRelease(surfaceTextureView);
#endif

    glfwPollEvents();
  }
}

void App::shutdown() {
  // TODO: Remove
  triangleShader.destroy();

  wgpuSurfaceRelease(sSurface);
  wgpuDeviceRelease(sDevice);

  glfwDestroyWindow(sWindow);
  glfwTerminate();
}

} // namespace cbz
