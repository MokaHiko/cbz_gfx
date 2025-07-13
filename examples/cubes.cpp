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

std::vector<float> sVertices = {
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

struct ColorRGBA8 {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

constexpr uint32_t kWidth = 1280;
constexpr uint32_t kHeight = 720;

class Cubes {
public:
  void init(cbz::NetworkStatus netStatus = cbz::NetworkStatus::eClient) {

    if (cbz::Init({"Cubes", kWidth, kHeight, netStatus}) !=
        cbz::Result::eSuccess) {
      exit(0);
    }

    mLitSH = cbz::ShaderCreate("assets/shaders/gltf_viewer.slang");
    mLitPH = cbz::GraphicsProgramCreate(mLitSH, "blit_program");

    cbz::VertexLayout layout;
    layout.begin(cbz::VertexStepMode::eVertex);
    layout.push_attribute(cbz::VertexAttributeType::ePosition,
                          cbz::VertexFormat::eFloat32x3);
    layout.push_attribute(cbz::VertexAttributeType::eNormal,
                          cbz::VertexFormat::eFloat32x3);
    layout.push_attribute(cbz::VertexAttributeType::eTexCoord0,
                          cbz::VertexFormat::eFloat32x2);
    layout.end();

    mQuadVBH = cbz::VertexBufferCreate(
        layout, static_cast<uint32_t>(sVertices.size()), sVertices.data());

    mQuadIBH = cbz::IndexBufferCreate(cbz::IndexFormat::eUint16,
                                      static_cast<uint32_t>(indices.size()),
                                      indices.data());

    mAlbedoTH = cbz::Texture2DCreate(cbz::TextureFormat::eRGBA8Unorm, kWidth,
                                     kHeight, "albedo");

    static std::array<ColorRGBA8, kWidth * kHeight> blit = {};
    for (uint32_t y = 0; y < kHeight; y++) {
      for (uint32_t x = 0; x < kWidth; x++) {
        blit[y * kWidth + x] =
            ColorRGBA8{static_cast<uint8_t>(x * 255 / (kWidth - 1)),
                       static_cast<uint8_t>(y * 255 / (kHeight - 1)), 0, 255};
      }
    }

    cbz::Texture2DUpdate(mAlbedoTH, blit.data(), kWidth * kHeight);

    mUniformUH = cbz::UniformCreate("uMyUniform", cbz::UniformType::eVec4);
  }

  void update() {

    struct {
      float time = glfwGetTime();
      uint32_t _padding[3]; // Uniform type is vec4
    } myUniform;

    cbz::UniformSet(mUniformUH, &myUniform);

    cbz::TextureSet(cbz::TextureSlot::e0, mAlbedoTH,
                    {cbz::FilterMode::eLinear, cbz::AddressMode::eClampToEdge});

    cbz::TextureSet(cbz::TextureSlot::e1, mAlbedoTH,
                    {cbz::FilterMode::eLinear, cbz::AddressMode::eClampToEdge});

    cbz::VertexBufferSet(mQuadVBH);
    cbz::IndexBufferSet(mQuadIBH);

    glm::mat4 transform = glm::mat4(1.0f);
    transform = glm::transpose(transform);
    cbz::TransformSet(glm::value_ptr(transform));

    cbz::Submit(0, mLitPH);
    cbz::Frame();
  }

  void shutdown() {
    cbz::TextureDestroy(mAlbedoTH);

    cbz::ShaderDestroy(mLitSH);
    cbz::GraphicsProgramDestroy(mLitPH);
    cbz::VertexBufferDestroy(mQuadVBH);
    cbz::IndexBufferDestroy(mQuadIBH);

    cbz::Shutdown();
  }

private:
  cbz::ShaderHandle mLitSH;
  cbz::GraphicsProgramHandle mLitPH;
  cbz::VertexBufferHandle mQuadVBH;
  cbz::IndexBufferHandle mQuadIBH;

  cbz::TextureHandle mAlbedoTH;
  cbz::UniformHandle mUniformUH;
};

int main(int argc, char **argv) {
  if (argc > 2) {
  }

  for (int i = 0; i < argc; i++) {
    printf("%s", argv[i]);
  }

  Cubes app = {};

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
        Cubes *app = reinterpret_cast<Cubes *>(userData);
        app->update();
      },
      (void *)&app, // value sent to the 'userData' arg of the callback
      0, true);
#endif

  app.shutdown();
}
