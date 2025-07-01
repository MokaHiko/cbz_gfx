#include "GLFW/glfw3.h"
#include "glm/matrix.hpp"
#include <cubozoa/cubozoa.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

#define GLM_FORCE_RIGHT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

std::vector<float> vertices = {
    // x,   y,     z,   normal,           uv
    -1.0f, 1.0f,  0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, // Vertex 1
    1.0f,  1.0f,  0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, // Vertex 2
    1.0f,  -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, // Vertex 4
    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // Vertex 3
};

std::vector<uint16_t> indices = {
    0, 1, 2, // Triangle #0 connects points #0, #1 and #2
    0, 2, 3  // Triangle #1 connects points #0, #2 and #3
};

class GltfViewer {
public:
  void init() {
    if (cbz::init({"GltfViewer", 1280, 720}) != cbz::Result::eSuccess) {
    }

    mLitSH = cbz::shaderCreate("assets/shaders/gltf_viewer.wgsl");
    mLitPH = cbz::graphicsProgramCreate(mLitSH);

    cbz::VertexLayout layout;
    layout.begin(cbz::VertexStepMode::eVertex);
    layout.push_attribute(cbz::VertexAttributeType::ePosition,
                          cbz::VertexFormat::eFloat32x3);
    layout.push_attribute(cbz::VertexAttributeType::eNormal,
                          cbz::VertexFormat::eFloat32x3);
    layout.push_attribute(cbz::VertexAttributeType::eTexCoord0,
                          cbz::VertexFormat::eFloat32x2);
    layout.end();

    mQuadVBH = cbz::vertexBufferCreate(
        layout, static_cast<uint32_t>(vertices.size()), vertices.data());

    mQuadIBH = cbz::indexBufferCreate(cbz::IndexFormat::eUint16,
                                      static_cast<uint32_t>(indices.size()),
                                      indices.data());

    mAlbedoTH =
        cbz::texture2DCreate(cbz::TextureFormat::eRGBA8Unorm, 1, 1, "albedo");

    std::array<uint8_t, 4> color = {255, 255, 255, 255};
    cbz::texture2DUpdate(mAlbedoTH, color.data(), 1);

    mAlbedoSamplerUH =
        cbz::uniformCreate("albedoSampler", cbz::UniformType::eSampler);
  }

  void update() {
    static glm::vec3 position{0.0};
    glm::mat4 model = glm::translate(glm::mat4(1.0), position);
    position.x += glfwGetTime() * 0.001f;

    glm::mat4 view = glm::lookAt(glm::vec3(0.0, 0.0f, 5.0f), glm::vec3(0.0),
                                 glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 proj =
        glm::perspective(glm::radians(90.0), 16.0 / 9.0, 0.1, 1000.0);

    glm::mat4 transform = proj * view * model;
    transform = glm::transpose(transform);
    cbz::transformBind(glm::value_ptr(transform));

    cbz::textureBind(0, mAlbedoTH, mAlbedoSamplerUH,
                     {cbz::FilterMode::Linear, cbz::AddressMode::ClampToEdge});

    cbz::vertexBufferBind(mQuadVBH);
    cbz::indexBufferBind(mQuadIBH);

    cbz::submit(0, mLitPH);
    cbz::frame();
  }

  void shutdown() {
    cbz::uniformDestroy(mAlbedoSamplerUH);
    cbz::textureDestroy(mAlbedoTH);

    cbz::shaderDestroy(mLitSH);
    cbz::graphicsProgramDestroy(mLitPH);
    cbz::vertexBufferDestroy(mQuadVBH);
    cbz::indexBufferDestroy(mQuadIBH);

    cbz::shutdown();
  }

private:
  cbz::ShaderHandle mLitSH;
  cbz::GraphicsProgramHandle mLitPH;
  cbz::VertexBufferHandle mQuadVBH;
  cbz::IndexBufferHandle mQuadIBH;

  cbz::TextureHandle mAlbedoTH;
  cbz::UniformHandle mAlbedoSamplerUH;
};

int main(int, char **) {
  GltfViewer app = {};
  app.init();

#ifndef __EMSCRIPTEN__
  while (true) {
    app.update();
  }
#else
  emscripten_set_main_loop_arg(
      [](void *userData) {
        static int i = 0;
        GltfViewer *app = reinterpret_cast<GltfViewer *>(userData);
        app->update();
      },
      (void *)&app, // value sent to the 'userData' arg of the callback
      0, true);
#endif

  app.shutdown();
}
