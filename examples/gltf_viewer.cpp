// ======================================================================================
// RTWeekend Example Renderer
// --------------------------------------------------------------------------------------
// This is a minimal example of voxel ray tracing using the Cubozoa (cbz)
// rendering API.
//
// Features:
// - Ray tracing implemented via compute shader (`voxel_raytracer.slang`)
// - Camera controls: WASD for horizontal movement, Space/Shift for vertical
// movement
// - Output written to a structured buffer as RGBA32F, then blitted to screen
// Purpose:
// - Demonstrates compute-based rendering pipeline
// - Serves as a reference for structured buffer usage, compute dispatch, and
// fullscreen blit
// ======================================================================================
#include "cbz_gfx/cbz_gfx_defines.h"
#include <cbz_gfx/cbz_gfx.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

#define GLM_FORCE_RIGHT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <cstdio>
#include <vector>

static std::vector<float> sQuadVertices = {
    // x,   y,     z,   uv
    -1.0f, 1.0f,  0.0f, 0.0f, 0.0f, // Vertex 1
    1.0f,  1.0f,  0.0f, 1.0f, 0.0f, // Vertex 2
    1.0f,  -1.0f, 0.0f, 1.0f, 1.0f, // Vertex 4
    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, // Vertex 3
};

static std::vector<uint16_t> sQuadIndices = {
    0, 1, 2, // Triangle #0 connects points #0, #1 and #2
    0, 2, 3  // Triangle #1 connects points #0, #2 and #3
};

struct ColorRGBA8 {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

static constexpr uint32_t kWidth = 854;
static constexpr uint32_t kHeight = 480;

constexpr glm::vec3 kVec3Up = {0.0, 1.0, 0.0};
constexpr glm::vec3 kVec3Right = {1.0, 0.0, 0.0};

class GLTFViewer {
public:
  GLTFViewer(CBZNetworkStatus netStatus = CBZ_NETWORK_CLIENT) {
    if (cbz::Init({"GLTFViewer", kWidth, kHeight, netStatus}) !=
        cbz::Result::eSuccess) {
      exit(0);
    }

    // --- Reset ---
    mLastTime = 0.0f;
    mTime = 1.0f;
    mFrameCtr = 0;
    mDeltaTime = 0.0f;

    // --- Blit Pipeline Setup ---
    // Create blit program
    mBlitSH = cbz::ShaderCreate("assets/shaders/lit.wgsl", CBZ_SHADER_WGLSL);
    mBlitPH = cbz::GraphicsProgramCreate(mBlitSH);

    // Create vertex layout
    cbz::VertexLayout layout = {};
    layout.begin(CBZ_VERTEX_STEP_MODE_VERTEX);
    layout.push_attribute(CBZ_VERTEX_ATTRIBUTE_POSITION,
                          CBZ_VERTEX_FORMAT_FLOAT32X3);
    layout.push_attribute(CBZ_VERTEX_ATTRIBUTE_TEXCOORD0,
                          CBZ_VERTEX_FORMAT_FLOAT32X2);
    layout.end();

    // Create full screen quad vertex and index buffers
    mQuadVBH = cbz::VertexBufferCreate(layout, 4, sQuadVertices.data());

    mQuadIBH =
        cbz::IndexBufferCreate(CBZ_INDEX_FORMAT_UINT16, 6, sQuadIndices.data());

    // Create blit texture
    mAlbedoTH =
        cbz::Image2DCreate(CBZ_TEXTURE_FORMAT_RGBA8UNORM, kWidth, kHeight);

    static std::array<ColorRGBA8, kWidth * kHeight> blit = {};
    for (uint32_t y = 0; y < kHeight; y++) {
      for (uint32_t x = 0; x < kWidth; x++) {
        blit[y * kWidth + x] =
            ColorRGBA8{static_cast<uint8_t>(x * 255 / (kWidth - 1)),
                       static_cast<uint8_t>(y * 255 / (kHeight - 1)), 0, 255};
      }
    }

    cbz::Image2DUpdate(mAlbedoTH, blit.data(), kWidth * kHeight);
  }

  ~GLTFViewer() {
    // Destroy common resources
    cbz::ImageDestroy(mAlbedoTH);

    // Destroy blit resources
    cbz::VertexBufferDestroy(mQuadVBH);
    cbz::IndexBufferDestroy(mQuadIBH);
    cbz::GraphicsProgramDestroy(mBlitPH);
    cbz::ShaderDestroy(mBlitSH);

    cbz::Shutdown();
    mDeltaTime = 0;
  }

  void update() {
    // mLastTime = mTime;
    // // mTime = glfwGetTime();
    // mDeltaTime = mTime - mLastTime;

    // // --- Blit pass ---
    for (int i = -5; i <= 5; i++) {
      // Set quad vertex and index buffers
      cbz::VertexBufferSet(mQuadVBH);
      cbz::IndexBufferSet(mQuadIBH);

      cbz::TextureSet(CBZ_TEXTURE_0, mAlbedoTH);

      // Set graphics transform to identity
      glm::mat4 model = glm::mat4(1.0f);
      model = glm::translate(model, glm::vec3(i * 3, 0.0f, -8.0f));

      model = glm::scale(model, glm::vec3(glm::sin((float)mFrameCtr * 0.05 *
                                                   glm::radians(30.0f))));

      glm::mat4 proj =
          glm::perspective(glm::radians(90.0f), 16.0f / 9.0f, 0.1f, 1000.0f);

      glm::mat4 transform = proj * model;
      cbz::TransformSet(glm::value_ptr(transform));

      // Submit to graphics target
      cbz::Submit(CBZ_DEFAULT_RENDER_TARGET, mBlitPH);
    }

    mFrameCtr = cbz::Frame();
  }

private:
  // Blit resources
  cbz::ShaderHandle mBlitSH;
  cbz::GraphicsProgramHandle mBlitPH;
  cbz::VertexBufferHandle mQuadVBH;
  cbz::IndexBufferHandle mQuadIBH;
  cbz::ImageHandle mAlbedoTH;

  // Application resources
  float mTime;
  float mLastTime;
  float mDeltaTime;
  uint32_t mFrameCtr;
};

int main(int argc, char **argv) {
  for (int i = 0; i < argc; i++) {
    printf("%s", argv[i]);
  }

  GLTFViewer app(argc > 1 ? CBZNetworkStatus::CBZ_NETWORK_HOST
                          : CBZNetworkStatus::CBZ_NETWORK_CLIENT);

#ifndef __EMSCRIPTEN__
  while (true) {
    app.update();
  }
#else
  emscripten_set_main_loop_arg(
      [](void *userData) {
        static int i = 0;
        GLTFViewer *app = reinterpret_cast<GLTFViewer *>(userData);
        app->update();
      },
      (void *)&app, // value sent to the 'userData' arg of the callback
      0, true);
#endif
}
