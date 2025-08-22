#include "cbz_gfx/cbz_gfx.h"

#include "cbz_gfx/cbz_gfx_defines.h"
#include "cbz_gfx/net/cbz_net.h"
#include "cbz_irenderer_context.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <GLFW/glfw3.h>
#include <cstdint>
#include <murmurhash/MurmurHash3.h>

namespace cbz {

// --- Application ---
static GLFWwindow *sWindow;
static std::shared_ptr<spdlog::logger> sLogger;

// --- Input ---
static std::array<CBZBool32, static_cast<uint32_t>(Key::eCount)>
    sLastFrameKeyMap;
static std::array<CBZBool32, static_cast<uint32_t>(Key::eCount)> sKeyMap;

static std::array<CBZBool32, static_cast<uint32_t>(MouseButton::eCount)>
    sLastFrameMouseButtonMap;
static std::array<CBZBool32, static_cast<uint32_t>(MouseButton::eCount)>
    sMouseButtonMap;

static MousePosition sMousePosition = {0, 0};
static double sMouseDeltaX = 0.0;
static double sMouseDeltaY = 0.0;
static double sMouseScrollDeltaX = 0.0;
static double sMouseScrollDeltaY = 0.0;

void SetInputMode(CBZInputMode inputMode) {
  glfwSetInputMode(sWindow, GLFW_CURSOR, inputMode);
}

static void InputKeyCallback(GLFWwindow *, int key, int, int action, int) {
  if (key == GLFW_KEY_UNKNOWN)
    return;

  if (action == GLFW_PRESS) {
    sKeyMap[key] = true;
  } else if (action == GLFW_RELEASE) {
    sKeyMap[key] = false;
  }
}

static void MouseButtonCallback(GLFWwindow *, int button, int action,
                                [[maybe_unused]] int mods) {
  if (action == GLFW_PRESS) {
    sMouseButtonMap[button] = true;
  } else if (action == GLFW_RELEASE) {
    sMouseButtonMap[button] = false;
  }
}

static void MouseMoveCallback(GLFWwindow *, double xpos, double ypos) {
  sMouseDeltaX = xpos - static_cast<double>(sMousePosition.x);
  sMouseDeltaY = ypos - static_cast<double>(sMousePosition.y);

  sMousePosition = {static_cast<uint32_t>(xpos), static_cast<uint32_t>(ypos)};
}

static void MouseScrollCallback(GLFWwindow *, double xoffset, double yoffset) {
  sMouseScrollDeltaX = xoffset;
  sMouseScrollDeltaY = yoffset;
}

void InputInit() {
  glfwSetKeyCallback(sWindow, InputKeyCallback);
  glfwSetMouseButtonCallback(sWindow, MouseButtonCallback);
  glfwSetCursorPosCallback(sWindow, MouseMoveCallback);
  glfwSetScrollCallback(sWindow, MouseScrollCallback);

  double xpos, ypos;
  glfwGetCursorPos(sWindow, &xpos, &ypos);
  sMousePosition = {static_cast<uint32_t>(xpos), static_cast<uint32_t>(ypos)};
}

void InputUpdate() {
  std::memcpy(sLastFrameKeyMap.data(), sKeyMap.data(),
              sizeof(CBZBool32) * sKeyMap.size());
  std::memcpy(sLastFrameMouseButtonMap.data(), sMouseButtonMap.data(),
              sizeof(CBZBool32) * sMouseButtonMap.size());

  // Clear deltas
  sMouseDeltaX = 0.0;
  sMouseDeltaY = 0.0;
  sMouseScrollDeltaX = 0.0;
  sMouseScrollDeltaY = 0.0;
}

CBZBool32 IsKeyDown(Key key) {
  if (static_cast<uint32_t>(key) >= static_cast<uint32_t>(Key::eCount)) {
    return false;
  }

  return static_cast<CBZBool32>(sKeyMap[static_cast<uint32_t>(key)]);
}

CBZBool32 IsKeyPressed(Key key) {
  if (static_cast<uint32_t>(key) >= static_cast<uint32_t>(Key::eCount)) {
    return false;
  }

  // Pressed this frame and was not pressed last frame
  return static_cast<CBZBool32>(sKeyMap[static_cast<uint32_t>(key)]) &&
         !static_cast<CBZBool32>(sLastFrameKeyMap[static_cast<uint32_t>(key)]);
}

MousePosition GetMousePosition() { return sMousePosition; }

CBZBool32 IsMouseButtonDown(MouseButton mouseButton) {
  if (static_cast<uint32_t>(mouseButton) >=
      static_cast<uint32_t>(MouseButton::eCount)) {
    return false;
  }

  return static_cast<CBZBool32>(
      sMouseButtonMap[static_cast<uint32_t>(mouseButton)]);
}

CBZBool32 IsMouseButtonPressed(MouseButton mouseButton) {
  if (static_cast<uint32_t>(mouseButton) >=
      static_cast<uint32_t>(MouseButton::eCount)) {
    return false;
  }

  // Pressed this frame and was not pressed last frame
  return static_cast<CBZBool32>(
             sMouseButtonMap[static_cast<uint32_t>(mouseButton)]) &&
         !static_cast<CBZBool32>(
             sLastFrameMouseButtonMap[static_cast<uint32_t>(mouseButton)]);
}

namespace input {

double GetAxis(Axis axis) {
  switch (axis) {
  case Axis::MouseX: {
    return sMouseDeltaX;
  } break;
  case Axis::MouseY: {
    return sMouseDeltaY;
  } break;
  }

  return 0;
}

}; // namespace input

// --- Renderer ---
struct TransformData {
  float transform[16];
  float view[16];
  float proj[16];

  float inverseTransform[16];
  float inverseView[16];
  float inverseProj[16];
};

// TODO: Safe draw count
static uint32_t sNextShaderProgramCmdIdx;
static std::vector<ShaderProgramCommand> sShaderProgramCmds;

static std::unique_ptr<cbz::IRendererContext> sRenderer;

static StructuredBufferHandle sTransformSBH;
static std::array<TransformData, MAX_COMMAND_SUBMISSIONS> sTransforms;

static std::vector<RenderTarget> sRenderTargets;

Result Init(InitDesc initDesc) {
  Result result = Result::eSuccess;

  sLogger = spdlog::stdout_color_mt("cbz");
  sLogger->set_level(spdlog::level::trace);
  sLogger->set_pattern("[%^%l%$][CBZ] %v");

  switch (initDesc.netStatus) {
  case CBZNetworkStatus::CBZ_NETWORK_CLIENT:
    result = net::initClient();
    break;
  case CBZNetworkStatus::CBZ_NETWORK_HOST:
    result = net::initServer();
    break;
  case CBZNetworkStatus::CBZ_NETWORK_NONE:
    result = Result::eNetworkFailure;
    break;
  }

  if (result != Result::eSuccess) {
    return result;
  };

  if (glfwInit() != GLFW_TRUE) {
    sLogger->critical("Failed to initialize glfw!");
    return Result::eGLFWError;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  sWindow = glfwCreateWindow(initDesc.width, initDesc.height, initDesc.name,
                             NULL, NULL);

  if (!sWindow) {
    sLogger->critical("Failed to create window!");
    glfwTerminate();
    return Result::eGLFWError;
  }

  InputInit();

  sRenderer = RendererContextCreate();
  if (sRenderer->init(initDesc.width, initDesc.height, sWindow,
                      HandleProvider<ImageHandle>::write(
                          "CurrentSurfaceImage")) != Result::eSuccess) {
    return Result::eFailure;
  }

  // Initialize transform array to identity
  TransformData data = {};
  data.transform[0] = 1;
  data.transform[5] = 1;
  data.transform[10] = 1;
  data.transform[15] = 1;
  data.view[0] = 1;
  data.view[5] = 1;
  data.view[10] = 1;
  data.view[15] = 1;
  data.proj[0] = 1;
  data.proj[5] = 1;
  data.proj[10] = 1;
  data.proj[15] = 1;
  sTransforms.fill(data);

  sTransformSBH = StructuredBufferCreate(
      CBZ_UNIFORM_TYPE_MAT4,
      MAX_COMMAND_SUBMISSIONS * (sizeof(TransformData) / (sizeof(float) * 16)),
      sTransforms.data());

  sShaderProgramCmds.resize(MAX_COMMAND_SUBMISSIONS);
  sNextShaderProgramCmdIdx = 0;

  return result;
}

VertexBufferHandle VertexBufferCreate(const VertexLayout &vertexLayout,
                                      uint32_t vertexCount, const void *data,
                                      const char *name) {
  VertexBufferHandle vbh = HandleProvider<VertexBufferHandle>::write(name);

  if (sRenderer->vertexBufferCreate(vbh, vertexLayout, vertexCount, data) !=
      Result::eSuccess) {
    HandleProvider<VertexBufferHandle>::free(vbh);
    return {CBZ_INVALID_HANDLE};
  }

  return vbh;
}

void VertexBufferUpdate(VertexBufferHandle vbh, uint32_t elementCount,
                        const void *data, uint32_t offset) {
  sRenderer->vertexBufferUpdate(vbh, elementCount, data, offset);
}

void VertexBufferSet(VertexBufferHandle vbh, uint32_t instances) {
  ShaderProgramCommand &cmd = sShaderProgramCmds[sNextShaderProgramCmdIdx];

  if (cmd.program.graphics.vbCount >= MAX_VERTEX_INPUT_BINDINGS) {
    sLogger->error("Surpassed max vertex input bindings of {}",
                   static_cast<uint32_t>(MAX_VERTEX_INPUT_BINDINGS));
    return;
  }

  cmd.program.graphics.instances = instances;
  cmd.program.graphics.vbhs[cmd.program.graphics.vbCount++] = vbh;
}

void VertexBufferDestroy(VertexBufferHandle vbh) {
  if (!HandleProvider<VertexBufferHandle>::isValid(vbh)) {
    sLogger->warn("Attempting to destroy invalid 'VertexBufferHandle'!");
    return;
  }

  sRenderer->vertexBufferDestroy(vbh);
  HandleProvider<VertexBufferHandle>::free(vbh);
}

IndexBufferHandle IndexBufferCreate(CBZIndexFormat format, uint32_t count,
                                    const void *data, const char *name) {
  IndexBufferHandle ibh = HandleProvider<IndexBufferHandle>::write(name);

  if (sRenderer->indexBufferCreate(ibh, format, count, data) !=
      Result::eSuccess) {
    HandleProvider<IndexBufferHandle>::free(ibh);
    return {CBZ_INVALID_HANDLE};
  }

  return ibh;
}

void IndexBufferSet(IndexBufferHandle ibh) {
  sShaderProgramCmds[sNextShaderProgramCmdIdx].program.graphics.ibh = ibh;
}

void IndexBufferDestroy(IndexBufferHandle ibh) {
  if (!HandleProvider<IndexBufferHandle>::isValid(ibh)) {
    sLogger->warn("Attempting to destroy invalid 'VertexBufferHandle'!");
    return;
  }

  sRenderer->indexBufferDestroy(ibh);
  HandleProvider<IndexBufferHandle>::free(ibh);
}

StructuredBufferHandle StructuredBufferCreate(CBZUniformType type,
                                              uint32_t elementCount,
                                              const void *elementData,
                                              int flags, const char *name) {
  StructuredBufferHandle sbh =
      HandleProvider<StructuredBufferHandle>::write(name);

  if (sRenderer->structuredBufferCreate(sbh, type, elementCount, elementData,
                                        flags) != Result::eSuccess) {

    HandleProvider<StructuredBufferHandle>::free(sbh);
    return {CBZ_INVALID_HANDLE};
  }

  return sbh;
}

void StructuredBufferUpdate(StructuredBufferHandle sbh, uint32_t elementCount,
                            const void *data, uint32_t offset) {
  sRenderer->structuredBufferUpdate(sbh, elementCount, data, offset);
}

void StructuredBufferSet(CBZBufferSlot slot, StructuredBufferHandle sbh,
                         CBZBool32 dynamic) {
  Binding binding = {};

  binding.type = dynamic ? BindingType::eRWStructuredBuffer
                         : BindingType::eStructuredBuffer;

  binding.value.storageBuffer.slot = static_cast<uint8_t>(slot);
  binding.value.storageBuffer.handle = sbh;

  sShaderProgramCmds[sNextShaderProgramCmdIdx].bindings.push_back(binding);
}

void StructuredBufferDestroy(StructuredBufferHandle sbh) {
  if (HandleProvider<StructuredBufferHandle>::isValid(sbh)) {
    sRenderer->structuredBufferDestroy(sbh);
    HandleProvider<StructuredBufferHandle>::free(sbh);
  }
}

UniformHandle UniformCreate(const char *name, CBZUniformType type,
                            uint16_t elementCount) {
  UniformHandle uh = HandleProvider<UniformHandle>::write(name);

  switch (type) {
  case CBZ_UNIFORM_TYPE_VEC4:
  case CBZ_UNIFORM_TYPE_MAT4: {
    if (sRenderer->uniformBufferCreate(uh, type, elementCount) !=
        Result::eSuccess) {
      HandleProvider<UniformHandle>::free(uh);
      return {CBZ_INVALID_HANDLE};
    }
  } break;
  default:
    break;
  }

  return uh;
}

void UniformSet(UniformHandle uh, const void *data, uint16_t num) {
  if (!HandleProvider<UniformHandle>::isValid(uh)) {
    sLogger->error("Attempting to set uniform with invalid handle!");
    return;
  }

  // TODO: Allow uniform update between submissions
  sRenderer->uniformBufferUpdate(uh, data, num);

  Binding binding = {};
  binding.type = BindingType::eUniformBuffer;
  binding.value.uniformBuffer.handle = uh;

  sShaderProgramCmds[sNextShaderProgramCmdIdx].bindings.push_back(binding);
}

void UniformDestroy(UniformHandle uh) {
  if (!HandleProvider<UniformHandle>::isValid(uh)) {
    sLogger->warn("Attempting to destroy uniform with invalid handle!");
    return;
  }

  sRenderer->uniformBufferDestroy(uh);
  HandleProvider<UniformHandle>::free(uh);
}

ImageHandle Image2DCreate(CBZTextureFormat format, uint32_t w, uint32_t h,
                          int flags) {
  ImageHandle uh = HandleProvider<ImageHandle>::write();

  // Prefer uniform buffer
  if (sRenderer->imageCreate(uh, format, w, h, 1, CBZ_TEXTURE_DIMENSION_2D,
                             static_cast<CBZImageFlags>(flags)) !=
      Result::eSuccess) {
    HandleProvider<ImageHandle>::free(uh);
    return {CBZ_INVALID_HANDLE};
  }

  return uh;
}

ImageHandle Image2DCubeMapCreate(CBZTextureFormat format, uint32_t w,
                                 uint32_t h, uint32_t depth, int flags) {
  ImageHandle uh = HandleProvider<ImageHandle>::write();

  // Prefer uniform buffer
  if (sRenderer->imageCreate(uh, format, w, h, depth, CBZ_TEXTURE_DIMENSION_2D,
                             static_cast<CBZImageFlags>(flags)) !=
      Result::eSuccess) {
    HandleProvider<ImageHandle>::free(uh);
    return {CBZ_INVALID_HANDLE};
  }

  return uh;
}

void ImageSetName(ImageHandle imgh, const char *name, uint32_t len) {
  if (!HandleProvider<ImageHandle>::isValid(imgh)) {
    sLogger->error("Attempting to name invalid image handle!");
    return;
  }

  HandleProvider<ImageHandle>::setName(imgh, std::string(name, len));
}

void Image2DUpdate(ImageHandle th, void *data, uint32_t count) {
  sRenderer->imageUpdate(th, data, count);
}

static void SamplerBind(CBZTextureSlot textureSlot, TextureBindingDesc desc) {
  Binding binding = {};
  binding.type = BindingType::eSampler;
  binding.value.sampler.slot = static_cast<uint8_t>(textureSlot) + 1;
  binding.value.sampler.handle = sRenderer->getSampler(desc);

  sShaderProgramCmds[sNextShaderProgramCmdIdx].bindings.push_back(binding);
}

void TextureSet(CBZTextureSlot slot, ImageHandle th, TextureBindingDesc desc) {
  if (th.idx == CBZ_INVALID_HANDLE) {
    sLogger->error("Attempting to bind invalid handle at texture slot @{}!",
                   static_cast<uint32_t>(slot));
    return;
  }

  Binding binding = {};
  switch (desc.viewDimension) {
  case (CBZ_TEXTURE_VIEW_DIMENSION_2D): {
    binding.type = BindingType::eTexture2D;
  } break;

  case (CBZ_TEXTURE_VIEW_DIMENSION_CUBE): {
    binding.type = BindingType::eTextureCube;
  } break;
  }

  binding.value.texture.slot = static_cast<uint8_t>(slot);
  binding.value.texture.handle = th;
  sShaderProgramCmds[sNextShaderProgramCmdIdx].bindings.push_back(binding);

  if (desc.addressMode != CBZ_ADDRESS_MODE_COUNT) {
    SamplerBind(slot, desc);
  }
}

void ImageDestroy(ImageHandle imgh) {
  if (!HandleProvider<ImageHandle>::isValid(imgh)) {
    sLogger->warn("Attempting to destroy invalid 'ImageHandle'!");
    return;
  }

  sRenderer->imageDestroy(imgh);
  HandleProvider<ImageHandle>::free(imgh);
}

ShaderHandle ShaderCreate(const char *path, int flags) {
  ShaderHandle sh = HandleProvider<ShaderHandle>::write();

  if (sRenderer->shaderCreate(sh, static_cast<CBZShaderFlags>(flags), path) !=
      Result::eSuccess) {
    sLogger->error("Failed to create shader module!");
    HandleProvider<ShaderHandle>::free(sh);
    return {CBZ_INVALID_HANDLE};
  }

  return sh;
}

void ShaderSetName(ShaderHandle sh, const char *name, uint32_t len) {
  if (!HandleProvider<ShaderHandle>::isValid(sh)) {
    sLogger->error("Attempting to name invalid shader handle!");
    return;
  }

  HandleProvider<ShaderHandle>::setName(sh, std::string(name, len));
}

void ShaderDestroy(ShaderHandle sh) {
  if (HandleProvider<ShaderHandle>::isValid(sh)) {
    sRenderer->shaderDestroy(sh);
    HandleProvider<ShaderHandle>::free(sh);
  }
}

GraphicsProgramHandle GraphicsProgramCreate(ShaderHandle sh, int flags) {
  if (sh.idx == CBZ_INVALID_HANDLE) {
    sLogger->error(
        "Attempting to create graphics program with invalid shader handle!");
    return {CBZ_INVALID_HANDLE};
  }

  GraphicsProgramHandle gph = HandleProvider<GraphicsProgramHandle>::write();

  if (sRenderer->graphicsProgramCreate(gph, sh, flags) != Result::eSuccess) {
    HandleProvider<GraphicsProgramHandle>::free(gph);
    return {CBZ_INVALID_HANDLE};
  }

  return gph;
}

void GraphicsProgramSetName(GraphicsProgramHandle gph, const char *name,
                            uint32_t len) {
  if (!HandleProvider<GraphicsProgramHandle>::isValid(gph)) {
    sLogger->error("Attempting to name invalid graphics program handle!");
    return;
  }

  HandleProvider<GraphicsProgramHandle>::setName(gph, std::string(name, len));
}

void GraphicsProgramDestroy(GraphicsProgramHandle gph) {
  if (!HandleProvider<GraphicsProgramHandle>::isValid(gph)) {
    sLogger->warn("Attempting to destroy invalid 'GraphicsProgramHandle'!");
    return;
  }

  sRenderer->graphicsProgramDestroy(gph);
  HandleProvider<GraphicsProgramHandle>::free(gph);
}

ComputeProgramHandle ComputeProgramCreate(ShaderHandle sh, const char *name) {
  ComputeProgramHandle cph = HandleProvider<ComputeProgramHandle>::write(name);

  if (sRenderer->computeProgramCreate(cph, sh) != Result::eSuccess) {
    HandleProvider<ComputeProgramHandle>::free(cph);
    return {CBZ_INVALID_HANDLE};
  }

  return cph;
}

void ComputeProgramDestroy(ComputeProgramHandle cph) {
  if (HandleProvider<ComputeProgramHandle>::isValid(cph)) {
    sRenderer->computeProgramDestroy(cph);
    HandleProvider<ComputeProgramHandle>::free(cph);
  }
}

void TransformSet(const float *transform) {
  memcpy(&sTransforms[sNextShaderProgramCmdIdx].transform, transform,
         sizeof(float) * 16);

  glm::mat4 inverseTransform = glm::inverse(glm::make_mat4(transform));
  memcpy(&sTransforms[sNextShaderProgramCmdIdx].inverseTransform,
         glm::value_ptr(inverseTransform), sizeof(float) * 16);
}

void ViewSet(const float *view) {
  memcpy(&sTransforms[sNextShaderProgramCmdIdx].view, view, sizeof(float) * 16);

  glm::mat4 inverseView = glm::inverse(glm::make_mat4(view));
  memcpy(&sTransforms[sNextShaderProgramCmdIdx].inverseView,
         glm::value_ptr(inverseView), sizeof(float) * 16);
}

void ProjectionSet(const float *proj) {
  memcpy(&sTransforms[sNextShaderProgramCmdIdx].proj, proj, sizeof(float) * 16);

  glm::mat4 inverseProj = glm::inverse(glm::make_mat4(proj));
  memcpy(&sTransforms[sNextShaderProgramCmdIdx].inverseProj,
         glm::value_ptr(inverseProj), sizeof(float) * 16);
}

void RenderTargetSet(uint8_t target,
                     const AttachmentDescription *colorAttachments,
                     uint32_t colorAttachmentCount,
                     const AttachmentDescription *depthAttachment) {
  if (target >= sRenderTargets.size()) {
    sRenderTargets.resize(target + 1u);
  }

  sRenderTargets[target].colorAttachments.clear();
  for (uint32_t i = 0; i < colorAttachmentCount; i++) {
    sRenderTargets[target].colorAttachments.emplace_back(colorAttachments[i]);
  }

  if (depthAttachment) {
    sRenderTargets[target].depthAttachment = *depthAttachment;
  }
}

void Submit(uint8_t target, GraphicsProgramHandle gph) {
  if (gph.idx == CBZ_INVALID_HANDLE) {
    sLogger->critical("Attempting to submit with invalid program handle!");
    exit(0);
    return;
  }

  // TODO : Target program compatiblity check.
  if (sShaderProgramCmds.size() > MAX_COMMAND_SUBMISSIONS) {
    sLogger->error("Application has exceeded maximum draw calls {}!",
                   static_cast<uint32_t>(MAX_COMMAND_SUBMISSIONS));
    return;
  }

  StructuredBufferSet(CBZ_BUFFER_GLOBAL_TRANSFORM, sTransformSBH);

  ShaderProgramCommand *currentCommand =
      &sShaderProgramCmds[sNextShaderProgramCmdIdx];

  if (currentCommand->bindings.size() >
      static_cast<uint64_t>(MAX_COMMAND_BINDINGS)) {
    sLogger->error("Draw called exceeding max bindings {} > {}",
                   currentCommand->bindings.size(), sNextShaderProgramCmdIdx);
    sNextShaderProgramCmdIdx++;
    return;
  }

  currentCommand->programType = CBZ_TARGET_TYPE_GRAPHICS;

  uint32_t uniformHash;
  MurmurHash3_x86_32(currentCommand->bindings.data(),
                     static_cast<uint32_t>(currentCommand->bindings.size()) *
                         sizeof(Binding),
                     0, &uniformHash);

  currentCommand->program.graphics.ph = gph;

  currentCommand->target = target;

  currentCommand->sortKey =
      (uint64_t)(gph.idx & 0xFFFF) << 48 |
      (uint64_t)(currentCommand->program.graphics.vbhs[0].idx & 0xFFFF) << 32 |
      (uint64_t)(uniformHash & 0xFFFFFFFF);
  currentCommand->submissionID = sNextShaderProgramCmdIdx++;
}

void Submit(uint8_t target, ComputeProgramHandle cph, uint32_t x, uint32_t y,
            uint32_t z) {
  // TODO : Target program compatiblity check.
  if (sShaderProgramCmds.size() > MAX_COMMAND_SUBMISSIONS) {
    sLogger->error("Application has exceeded maximum submits calls!");
    return;
  }

  ShaderProgramCommand *currentCommand =
      &sShaderProgramCmds[sNextShaderProgramCmdIdx];

  if (currentCommand->bindings.size() > MAX_COMMAND_BINDINGS) {
    sLogger->error("Draw called exceeding max uniform binds {} > {}",
                   currentCommand->bindings.size(), sNextShaderProgramCmdIdx);
    return;
  }

  currentCommand->programType = CBZ_TARGET_TYPE_COMPUTE;

  uint32_t uniformHash;
  MurmurHash3_x86_32(currentCommand->bindings.data(),
                     static_cast<uint32_t>(currentCommand->bindings.size()) *
                         sizeof(Binding),
                     0, &uniformHash);

  currentCommand->program.compute.x = x;
  currentCommand->program.compute.y = y;
  currentCommand->program.compute.z = z;
  currentCommand->program.compute.ph = cph;

  currentCommand->target = target;
  currentCommand->sortKey =
      (uint64_t)(cph.idx & 0xFFFF) << 48 |
      // (uint64_t)(currentCommand->program.graphics.vbh.idx & 0xFFFF) << 32 |
      (uint64_t)(uniformHash & 0xFFFFFFFF);
  currentCommand->submissionID = sNextShaderProgramCmdIdx++;
}

void ReadBufferAsync(StructuredBufferHandle sbh,
                     std::function<void(const void *data)> callback) {
  if (sbh.idx == CBZ_INVALID_HANDLE) {
    sLogger->error("Attempting to read buffer with invalid handle!");
    return;
  }

  sRenderer->readBufferAsync(sbh, callback);
}

void TextureReadAsync(ImageHandle imgh, const Origin3D *origin,
                      const TextureExtent *extent,
                      std::function<void(const void *data)> callback) {
  if (imgh.idx == CBZ_INVALID_HANDLE) {
    sLogger->error("Attempting to read buffer with invalid handle!");
    return;
  }

  sRenderer->textureReadAsync(imgh, origin, extent, callback);
}

uint32_t Frame() {
  InputUpdate();

  const uint32_t submissionCount = sNextShaderProgramCmdIdx;

  if (submissionCount > 0) {
    const uint32_t mat4PerTransform =
        (sizeof(TransformData) / (sizeof(float) * 16));
    StructuredBufferUpdate(sTransformSBH, submissionCount * mat4PerTransform,
                           sTransforms.data());
  }

  std::sort(sShaderProgramCmds.begin(),
            sShaderProgramCmds.begin() + submissionCount,
            [](const ShaderProgramCommand &a, const ShaderProgramCommand &b) {
              if (a.target != b.target) {
                return a.target < b.target;
              }

              return a.sortKey < b.sortKey;
            });

  uint32_t frameIdx = sRenderer->submitSorted(
      sRenderTargets, sShaderProgramCmds.data(), submissionCount);

  // Clear submissions
  for (uint32_t i = 0; i < sNextShaderProgramCmdIdx; i++) {
    // Clear program data
    memset(&sShaderProgramCmds[i].program, 0,
           sizeof(sShaderProgramCmds[i].program));
    sShaderProgramCmds[i].programType = CBZ_TARGET_TYPE_NONE;

    // Clear binding data
    sShaderProgramCmds[i].bindings.clear();

    // Set sort key to invalid
    sShaderProgramCmds[i].sortKey = std::numeric_limits<uint64_t>::max();
  }
  sNextShaderProgramCmdIdx = 0;

  glfwPollEvents();
  if (glfwWindowShouldClose(sWindow)) {
    exit(0);
  };

  return frameIdx;
}

void Shutdown() {
  StructuredBufferDestroy(sTransformSBH);

  sRenderer->shutdown();

  glfwDestroyWindow(sWindow);
  glfwTerminate();
}

void VertexLayout::begin(CBZVertexStepMode mode) {
  if (attributes.size() > 0 || stride != 0) {
    spdlog::warn(
        "VertexLayout::begin() called with non empty attribute array!");
  }

  stepMode = mode;
  stride = 0;
}

void VertexLayout::push_attribute(CBZVertexAttributeType,
                                  CBZVertexFormat format,
                                  uint32_t locationOffset) {
  attributes.push_back(
      {format, stride,
       static_cast<uint32_t>(attributes.size()) + locationOffset});

  stride += VertexFormatGetSize(format);
}

void VertexLayout::end() {
  // TODO: Check if layout is valid
  // for (VertexAttribute & attrib : attributes) { }
}

bool VertexLayout::operator==(const VertexLayout &other) const {
  if (attributes.size() != other.attributes.size()) {
    return false;
  }

  if (stride != other.stride) {
    return false;
  }

  if ((uint32_t)stepMode != (uint32_t)other.stepMode) {
    return false;
  }

  for (uint64_t i = 0; i < attributes.size(); i++) {
    if (attributes[i].shaderLocation != other.attributes[i].shaderLocation) {
      return false;
    };

    if (attributes[i].format != other.attributes[i].format) {
      return false;
    };
  }

  return true;
}

bool VertexLayout::operator!=(const VertexLayout &other) const {
  return !(*this == other);
}

} // namespace cbz
