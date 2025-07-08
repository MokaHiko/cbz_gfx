#include "cubozoa/cubozoa.h"

#include "cubozoa/cubozoa_defines.h"
#include "cubozoa/net/cubozoa_net.h"
#include "renderer/cubozoa_irenderer_context.h"

#include <GLFW/glfw3.h>
#include <murmurhash/MurmurHash3.h>

namespace cbz {

void VertexLayout::begin(VertexStepMode mode) {
  if (attributes.size() > 0 || stride != 0) {
    spdlog::warn(
        "VertexLayout::begin() called with non empty attribute array!");
  }

  stepMode = mode;
  stride = 0;
}

void VertexLayout::push_attribute(VertexAttributeType _, VertexFormat format) {
  attributes.push_back(
      {format, stride, static_cast<uint32_t>(attributes.size())});

  stride += VertexFormatGetSize(format);
}

void VertexLayout::end() {
  for (VertexAttribute &_ : attributes) {
  }
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

struct TransformData {
  float transform[16];
  float view[16];
  float proj[16];

  float inverseTransform[16];
  float inverseView[16];
};

static GLFWwindow *sWindow;
static std::shared_ptr<spdlog::logger> sLogger;

static std::vector<ShaderProgramCommand> sShaderProgramCmds;
static uint32_t sNextShaderProgramCmdIdx;

static std::unique_ptr<cbz::IRendererContext> sRenderer;

static StructuredBufferHandle sTransformSBH;
static std::array<TransformData, MAX_COMMAND_SUBMISSIONS> sTransforms;

Result init(InitDesc initDesc) {
  Result res;

  switch (initDesc.netStatus) {
  case NetworkStatus::eClient:
    res = net::initClient();
    break;
  case NetworkStatus::eHost:
    res = net::initServer();
    break;
  case NetworkStatus::eNone:
    res = Result::eNetworkFailure;
    break;
  }

  if (res != Result::eSuccess) {
    return res;
  };

  sLogger = spdlog::stdout_color_mt("cbz");
  sLogger->set_level(spdlog::level::trace);
  sLogger->set_pattern("[%^%l%$][CBZ] %v");

  if (glfwInit() != GLFW_TRUE) {
    sLogger->error("Failed to initialize glfw!");
    return Result::eGLFWError;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  sWindow = glfwCreateWindow(initDesc.width, initDesc.height, initDesc.name,
                             NULL, NULL);

  if (!sWindow) {
    spdlog::error("Failed to create window!");
    glfwTerminate();
    return Result::eGLFWError;
  }

  sRenderer = RendererContextCreate();
  if (sRenderer->init(initDesc.width, initDesc.height, sWindow) !=
      Result::eSuccess) {
    return Result::eFailure;
  }

  TransformData data = {};
  data.transform[0] = 1;
  data.transform[5] = 1;
  data.transform[10] = 1;
  data.transform[15] = 1;
  sTransforms.fill(data);
  sTransformSBH = StructuredBufferCreate(
      UniformType::eMat4,
      MAX_COMMAND_SUBMISSIONS * (sizeof(TransformData) / (sizeof(float) * 16)),
      sTransforms.data());

  sShaderProgramCmds.resize(2);
  sNextShaderProgramCmdIdx = 0;
  return Result::eSuccess;
}

VertexBufferHandle VertexBufferCreate(const VertexLayout &vertexLayout,
                                      uint32_t count, const void *data,
                                      const std::string &name) {
  VertexBufferHandle vbh = HandleProvider<VertexBufferHandle>::write(name);

  if (sRenderer->vertexBufferCreate(vbh, vertexLayout, count, data) !=
      Result::eSuccess) {
    HandleProvider<VertexBufferHandle>::free(vbh);
    return {CBZ_INVALID_HANDLE};
  }

  return vbh;
}

void VertexBufferSet(VertexBufferHandle vbh) {
  sShaderProgramCmds[sNextShaderProgramCmdIdx].program.graphics.vbh = vbh;
}

void VertexBufferDestroy(VertexBufferHandle vbh) {
  if (HandleProvider<VertexBufferHandle>::isValid(vbh)) {
    sRenderer->vertexBufferDestroy(vbh);
    HandleProvider<VertexBufferHandle>::free(vbh);
  }
}

IndexBufferHandle IndexBufferCreate(IndexFormat format, uint32_t count,
                                    const void *data, const std::string &name) {
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
  if (HandleProvider<IndexBufferHandle>::isValid(ibh)) {
    sRenderer->indexBufferDestroy(ibh);
    HandleProvider<IndexBufferHandle>::free(ibh);
  }
}

StructuredBufferHandle StructuredBufferCreate(UniformType type, uint32_t num,
                                              const void *data,
                                              const std::string &name) {
  StructuredBufferHandle sbh =
      HandleProvider<StructuredBufferHandle>::write(name);

  if (sRenderer->structuredBufferCreate(sbh, type, num, data) !=
      Result::eSuccess) {

    HandleProvider<StructuredBufferHandle>::free(sbh);
    return {CBZ_INVALID_HANDLE};
  }

  return sbh;
}

void StructuredBufferSet(BufferSlot slot, StructuredBufferHandle sbh) {
  Binding binding = {};
  binding.type = BindingType::eStructuredBuffer;

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

UniformHandle UniformCreate(const std::string &name, UniformType type,
                            uint16_t num) {
  UniformHandle uh = HandleProvider<UniformHandle>::write(name);

  switch (type) {
  case UniformType::eVec4:
  case UniformType::eMat4: {
    if (sRenderer->uniformBufferCreate(uh, type, num) != Result::eSuccess) {
      HandleProvider<UniformHandle>::free(uh);
      return {CBZ_INVALID_HANDLE};
    }
  } break;
  default:
    break;
  }

  return uh;
}

void UniformSet(UniformHandle uh, void *data, uint16_t num) {
  if (uh.idx == CBZ_INVALID_HANDLE) {
    sLogger->warn("Attempting to set uniform with invalid handle!");
    return;
  }

  sRenderer->uniformBufferUpdate(uh, data, num);

  Binding binding = {};
  binding.type = BindingType::eUniformBuffer;
  binding.value.uniformBuffer.handle = uh;

  sShaderProgramCmds[sNextShaderProgramCmdIdx].bindings.push_back(binding);
}

void UniformDestroy(UniformHandle uh) {
  if (HandleProvider<UniformHandle>::isValid(uh)) {
    sRenderer->uniformBufferDestroy(uh);
    HandleProvider<UniformHandle>::free(uh);
  }
}

TextureHandle Texture2DCreate(TextureFormat format, uint32_t w, uint32_t h,
                              const std::string &name) {
  TextureHandle uh = HandleProvider<TextureHandle>::write(name);

  // Prefer uniform buffer
  if (sRenderer->textureCreate(uh, format, w, h, 1, TextureDimension::e2D) !=
      Result::eSuccess) {
    HandleProvider<TextureHandle>::free(uh);
    return {CBZ_INVALID_HANDLE};
  }

  return uh;
}

void Texture2DUpdate(TextureHandle th, void *data, uint32_t count) {
  sRenderer->textureUpdate(th, data, count);
}

static void SamplerBind(TextureSlot textureSlot, TextureBindingDesc desc) {
  Binding binding = {};
  binding.type = BindingType::eSampler;
  binding.value.sampler.slot = static_cast<uint8_t>(textureSlot) + 1;
  binding.value.sampler.handle = sRenderer->getSampler(desc);

  sShaderProgramCmds[sNextShaderProgramCmdIdx].bindings.push_back(binding);
}

void TextureSet(TextureSlot slot, TextureHandle th, TextureBindingDesc desc) {
  Binding binding = {};
  binding.type = BindingType::eTexture2D;
  binding.value.texture.slot = static_cast<uint8_t>(slot);
  binding.value.texture.handle = th;
  sShaderProgramCmds[sNextShaderProgramCmdIdx].bindings.push_back(binding);

  if (desc.addressMode != AddressMode::eCount) {
    SamplerBind(slot, desc);
  }
}

void TextureDestroy(TextureHandle th) {
  if (!HandleProvider<TextureHandle>::isValid(th)) {
    sLogger->warn("Attempting to destroy invalid 'TextureHandle'!");
    return;
  }

  sRenderer->textureDestroy(th);
  HandleProvider<TextureHandle>::free(th);
}

ShaderHandle ShaderCreate(const std::string &path, const std::string &name) {
  ShaderHandle sh = HandleProvider<ShaderHandle>::write(name);

  if (sRenderer->shaderCreate(sh, path) != Result::eSuccess) {
    sLogger->error("Failed to create shader module!");
    HandleProvider<ShaderHandle>::free(sh);
    return {CBZ_INVALID_HANDLE};
  }

  return sh;
}

void ShaderDestroy(ShaderHandle sh) {
  if (HandleProvider<ShaderHandle>::isValid(sh)) {
    sRenderer->shaderDestroy(sh);
    HandleProvider<ShaderHandle>::free(sh);
  }
}

GraphicsProgramHandle GraphicsProgramCreate(ShaderHandle sh,
                                            const std::string &name) {
  GraphicsProgramHandle gph =
      HandleProvider<GraphicsProgramHandle>::write(name);

  if (sRenderer->graphicsProgramCreate(gph, sh) != Result::eSuccess) {
    HandleProvider<GraphicsProgramHandle>::free(gph);
    return {CBZ_INVALID_HANDLE};
  }

  return gph;
}

void GraphicsProgramDestroy(GraphicsProgramHandle gph) {
  if (!HandleProvider<GraphicsProgramHandle>::isValid(gph)) {
    sLogger->warn("Attempting to destroy invalid 'GraphicsProgramHandle'!");
    return;
  }

  sRenderer->graphicsProgramDestroy(gph);
  HandleProvider<GraphicsProgramHandle>::free(gph);
}

ComputeProgramHandle ComputeProgramCreate(ShaderHandle sh,
                                          const std::string &name) {
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

void TransformSet(float *transform) {
  memcpy(&sTransforms[sShaderProgramCmds.size()], transform,
         sizeof(float) * 16);
}

void Submit(uint8_t _, GraphicsProgramHandle gph) {
  if (sShaderProgramCmds.size() > MAX_COMMAND_SUBMISSIONS) {
    sLogger->error("Application has exceeded maximum draw calls! Consider "
                   "batching or instancing.");
    return;
  }

  // StructuredBufferSet(BufferSlot::e0, sTransformSBH, sTransforms.data());
  StructuredBufferSet(BufferSlot::e0, sTransformSBH);

  if (sNextShaderProgramCmdIdx >= MAX_COMMAND_BINDINGS) {
    sLogger->error("Draw called exceeding max uniform binds {}",
                   sNextShaderProgramCmdIdx);
    return;
  }

  ShaderProgramCommand *currentCommand =
      &sShaderProgramCmds[sNextShaderProgramCmdIdx];
  currentCommand->programType = ProgramType::eGraphics;

  // TODO: Submit to target array
  uint32_t uniformHash;
  MurmurHash3_x86_32(currentCommand->bindings.data(),
                     static_cast<uint32_t>(currentCommand->bindings.size()), 0,
                     &uniformHash);

  currentCommand->program.graphics.ph = gph;
  currentCommand->sortKey =
      (uint64_t)(gph.idx & 0xFFFF) << 48 |
      (uint64_t)(currentCommand->program.graphics.vbh.idx & 0xFFFF) << 32 |
      (uint64_t)(uniformHash & 0xFFFFFFFF);

  currentCommand->id = static_cast<uint32_t>(sShaderProgramCmds.size());

  sNextShaderProgramCmdIdx++;
}

bool Frame() {
  std::sort(sShaderProgramCmds.begin(),
            sShaderProgramCmds.begin() + sNextShaderProgramCmdIdx,
            [](const ShaderProgramCommand &a, const ShaderProgramCommand &b) {
              return a.sortKey < b.sortKey;
            });

  sRenderer->submitSorted(sShaderProgramCmds.data(), sNextShaderProgramCmdIdx);

  for (uint32_t i = 0; i < sNextShaderProgramCmdIdx; i++) {
    sShaderProgramCmds[i].bindings.clear();
    sShaderProgramCmds[i].programType = ProgramType::eNone;
    sShaderProgramCmds[i].sortKey = std::numeric_limits<uint64_t>::max();
  }
  sNextShaderProgramCmdIdx = 0;

  glfwPollEvents();

  return !glfwWindowShouldClose(sWindow);
}

void Shutdown() {
  StructuredBufferDestroy(sTransformSBH);

  sRenderer->shutdown();

  glfwDestroyWindow(sWindow);
  glfwTerminate();
}

} // namespace cbz
