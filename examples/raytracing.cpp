#include <cubozoa/cubozoa.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

#define GLM_FORCE_RIGHT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <GLFW/glfw3.h>

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

struct Color {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

constexpr uint32_t kWidth = 1280;
constexpr uint32_t kHeight = 720;

class RTWeekend {
public:
  void init(cbz::NetworkStatus netStatus = cbz::NetworkStatus::eClient) {

    if (cbz::init({"RTWeekend", kWidth, kHeight, netStatus}) !=
        cbz::Result::eSuccess) {
      exit(0);
    }

    mLitSH = cbz::shaderCreate("assets/shaders/gltf_viewer.slang");
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

    mAlbedoTH = cbz::texture2DCreate(cbz::TextureFormat::eRGBA8Unorm, kWidth,
                                     kHeight, "albedo");

    mAlbedoSamplerUH =
        cbz::uniformCreate("albedoSampler", cbz::UniformType::eSampler);
  }

  void update() {
    static std::array<Color, kWidth * kHeight> blit = {};

    float s = sin(glfwGetTime());
    float c = cos(glfwGetTime());

    for (uint32_t y = 0; y < kHeight; y++) {
      for (uint32_t x = 0; x < kWidth; x++) {
        blit[y * kWidth + x] =
            Color{static_cast<uint8_t>(s * x * 255 / (kWidth - 1)),
                  static_cast<uint8_t>(c * y * 255 / (kHeight - 1)), 0, 255};
      }
    }

    cbz::texture2DUpdate(mAlbedoTH, blit.data(), kWidth * kHeight);

    glm::mat4 transform = glm::mat4(1.0f);
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

int main(int argc, char **argv) {
  if (argc > 2) {
  }

  for (int i = 0; i < argc; i++) {
    printf("%s", argv[i]);
  }

  RTWeekend app = {};

  if (argc > 1) {
    app.init(cbz::NetworkStatus::eHost);
  } else {
    app.init(cbz::NetworkStatus::eClient);
  }

#ifndef __EMSCRIPTEN__
  while (true) {
    app.update();
  }
#else
  emscripten_set_main_loop_arg(
      [](void *userData) {
        static int i = 0;
        RTWeekend *app = reinterpret_cast<RTWeekend *>(userData);
        app->update();
      },
      (void *)&app, // value sent to the 'userData' arg of the callback
      0, true);
#endif

  app.shutdown();
}
