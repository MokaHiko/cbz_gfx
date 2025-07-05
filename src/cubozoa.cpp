#include "cubozoa/cubozoa.h"

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
  // uint32_t attributeLocation = static_cast<uint32_t>(type);
  // if (attributeLocation >=
  // static_cast<uint32_t>(VertexAttributeType::eCount)) {
  //   spdlog::error("Uknown vertex attribute type!");
  //   return;
  // }
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

struct Draw {
  uint32_t id;
  uint32_t _padding;
};

struct TransformData {
  float transform[16];
  float view[16];
  float proj[16];

  float inverseTransform[16];
  float inverseView[16];
};

static GLFWwindow *sWindow;
static std::shared_ptr<spdlog::logger> sLogger;

static RenderCommand sRenderCommandState;
static std::vector<RenderCommand> sRenderCommands;

static std::unique_ptr<cbz::IRendererContext> sRenderer;

static UniformHandle sTransformsUH;
static std::array<TransformData, MAX_DRAW_CALLS> sTransforms;

Result init(InitDesc initDesc) {
  // Result res;
  //
  // switch (initDesc.netStatus) {
  // case NetworkStatus::eClient:
  //   res = net::initClient();
  //   break;
  // case NetworkStatus::eHost:
  //   res = net::initServer();
  //   break;
  // case NetworkStatus::eNone:
  //   res = Result::eNetworkFailure;
  //   break;
  // }
  //
  // if (res != Result::eSuccess) {
  //   return res;
  // };

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

  sTransformsUH = uniformCreate(
      "transforms", UniformType::eMat4,
      MAX_DRAW_CALLS * (sizeof(TransformData) / (sizeof(float) * 16)));

  return Result::eSuccess;
}

VertexBufferHandle vertexBufferCreate(const VertexLayout &vertexLayout,
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

void vertexBufferDestroy(VertexBufferHandle vbh) {
  if (HandleProvider<VertexBufferHandle>::isValid(vbh)) {
    sRenderer->vertexBufferDestroy(vbh);
    HandleProvider<VertexBufferHandle>::free(vbh);
  }
}

IndexBufferHandle indexBufferCreate(IndexFormat format, uint32_t count,
                                    const void *data, const std::string &name) {
  IndexBufferHandle ibh = HandleProvider<IndexBufferHandle>::write(name);

  if (sRenderer->indexBufferCreate(ibh, format, count, data) !=
      Result::eSuccess) {
    HandleProvider<IndexBufferHandle>::free(ibh);
    return {CBZ_INVALID_HANDLE};
  }

  return ibh;
}

void indexBufferDestroy(IndexBufferHandle ibh) {
  if (HandleProvider<IndexBufferHandle>::isValid(ibh)) {
    sRenderer->indexBufferDestroy(ibh);
    HandleProvider<IndexBufferHandle>::free(ibh);
  }
}

UniformHandle uniformCreate(const std::string &name, UniformType type,
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

  // Uniform does not create texture.
  case UniformType::eSampler:
  case UniformType::eTexture2D:
    break;
  }

  return uh;
}

void uniformBind(UniformHandle uh, void *data, uint16_t num) {
  sRenderer->uniformBufferUpdate(uh, data, num);

  uint32_t idx = sRenderCommandState.uniformCount++;

  if (idx >= MAX_DRAW_UNIFORMS) {
    sLogger->error("Draw called exceeding max uniform binds {}", idx);
    return;
  }

  UniformBinding binding = {};
  binding.type = UniformType::eMat4;
  binding.uniformBuffer.handle = uh;
  sRenderCommandState.uniforms[idx] = binding;
}

void uniformDestroy(UniformHandle uh) {
  if (HandleProvider<UniformHandle>::isValid(uh)) {
    sRenderer->uniformBufferDestroy(uh);
    HandleProvider<UniformHandle>::free(uh);
  }
}

TextureHandle texture2DCreate(TextureFormat format, uint32_t w, uint32_t h,
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

void texture2DUpdate(TextureHandle th, void *data, uint32_t count) {
  sRenderer->textureUpdate(th, data, count);
}

static void samplerBind(UniformHandle _, TextureBindingDesc desc) {
  uint32_t idx = sRenderCommandState.uniformCount++;

  if (idx >= MAX_DRAW_UNIFORMS) {
    sLogger->error("Draw called exceeding max uniform binds {}", idx);
    return;
  }

  UniformBinding binding = {};
  binding.type = UniformType::eSampler;
  binding.texture.samplerHandle = sRenderer->getSampler(desc);
  sRenderCommandState.uniforms[idx] = binding;
}

void textureBind(uint16_t _, TextureHandle th, UniformHandle samplerHandle,
                 TextureBindingDesc desc) {
  uint32_t idx = sRenderCommandState.uniformCount++;

  if (idx >= MAX_DRAW_UNIFORMS) {
    sLogger->error("Draw called exceeding max uniform binds {}", idx);
    return;
  }

  UniformBinding binding = {};
  binding.type = UniformType::eTexture2D;
  binding.texture.handle = th;
  sRenderCommandState.uniforms[idx] = binding;

  if (samplerHandle.idx != CBZ_INVALID_HANDLE) {
    samplerBind(samplerHandle, desc);
  }
}

void textureDestroy(TextureHandle th) {
  if (HandleProvider<TextureHandle>::isValid(th)) {
    sRenderer->textureDestroy(th);
    HandleProvider<TextureHandle>::free(th);
  }
}

ShaderHandle shaderCreate(const std::string &moduleName,
                          const std::string &name) {
  ShaderHandle sh = HandleProvider<ShaderHandle>::write(name);

  if (sRenderer->shaderCreate(sh, moduleName) != Result::eSuccess) {
    HandleProvider<ShaderHandle>::free(sh);
    return {CBZ_INVALID_HANDLE};
  }

  return sh;
}

void shaderDestroy(ShaderHandle sh) {
  if (HandleProvider<ShaderHandle>::isValid(sh)) {
    sRenderer->shaderDestroy(sh);
    HandleProvider<ShaderHandle>::free(sh);
  }
}

GraphicsProgramHandle graphicsProgramCreate(ShaderHandle sh,
                                            const std::string &name) {
  GraphicsProgramHandle gph =
      HandleProvider<GraphicsProgramHandle>::write(name);

  if (sRenderer->graphicsProgramCreate(gph, sh) != Result::eSuccess) {
    HandleProvider<GraphicsProgramHandle>::free(gph);
    return {CBZ_INVALID_HANDLE};
  }

  return gph;
}

void graphicsProgramDestroy(GraphicsProgramHandle gph) {
  if (HandleProvider<GraphicsProgramHandle>::isValid(gph)) {
    sRenderer->graphicsProgramDestroy(gph);
    HandleProvider<GraphicsProgramHandle>::free(gph);
  }
}

void transformBind(float *transform) {
  memcpy(&sTransforms[sRenderCommands.size()], transform, sizeof(float) * 16);
}

void vertexBufferBind(VertexBufferHandle vbh) { sRenderCommandState.vbh = vbh; }

void indexBufferBind(IndexBufferHandle ibh) { sRenderCommandState.ibh = ibh; }

void submit(uint8_t _, GraphicsProgramHandle gph) {
  if (sRenderCommands.size() > MAX_DRAW_CALLS) {
    sLogger->error("Application has exceeded maximum draw calls! Consider "
                   "batching or instancing.");
    return;
  }

  uniformBind(sTransformsUH, sTransforms.data());

  // TODO: Submit to target array
  uint32_t uniformHash;
  MurmurHash3_x86_32(sRenderCommandState.uniforms.data(),
                     sRenderCommandState.uniformCount, 0, &uniformHash);

  sRenderCommandState.gph = gph;
  sRenderCommandState.gph = gph;
  sRenderCommandState.sortKey = (uint64_t)(gph.idx & 0xFFFF) << 48 |
                                (uint64_t)(sRenderCommandState.vbh.idx & 0xFFFF)
                                    << 32 |
                                (uint64_t)(uniformHash & 0xFFFFFFFF);

  sRenderCommandState.drawId = static_cast<uint32_t>(sRenderCommands.size());
  sRenderCommands.push_back(sRenderCommandState);

  sRenderCommandState = {};
}

bool frame() {
  std::sort(sRenderCommands.begin(), sRenderCommands.end(),
            [](const RenderCommand &a, const RenderCommand &b) {
              return a.sortKey < b.sortKey;
            });

  sRenderer->drawSorted(sRenderCommands);
  sRenderCommands.clear();

  glfwPollEvents();

  return !glfwWindowShouldClose(sWindow);
}

void shutdown() {
  uniformDestroy(sTransformsUH);

  sRenderer->shutdown();

  glfwDestroyWindow(sWindow);
  glfwTerminate();
}

} // namespace cbz
