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
//
// Purpose:
// - Demonstrates compute-based rendering pipeline
// - Serves as a reference for structured buffer usage, compute dispatch, and
// fullscreen blit
// ======================================================================================

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

static std::vector<float> sQuadVertices = {
    // x,   y,     z,   normal,           uv
    -1.0f, 1.0f,  0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, // Vertex 1
    1.0f,  1.0f,  0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, // Vertex 2
    1.0f,  -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, // Vertex 4
    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // Vertex 3
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

class RTWeekend {
public:
  void init(cbz::NetworkStatus netStatus = cbz::NetworkStatus::eClient) {
    if (cbz::Init({"RTWeekend", kWidth, kHeight, netStatus}) !=
        cbz::Result::eSuccess) {
      exit(0);
    }

    mLastTime = 0.0f;
    mTime = glfwGetTime();
    mFrameCtr = 0;

    // --- Common resources ---
    // Create structured buffer with 'eRGBA32Float' color values
    mImageSBH = cbz::StructuredBufferCreate(cbz::UniformType::eVec4,
                                            kWidth * kHeight, nullptr);

    // --- Blit Pipeline Setup ---
    // Create blit program
    mBlitSH = cbz::ShaderCreate("assets/shaders/blit.slang");
    mBlitPH = cbz::GraphicsProgramCreate(mBlitSH, "blit_program");

    // Create vertex layout
    cbz::VertexLayout layout;
    layout.begin(cbz::VertexStepMode::eVertex);
    layout.push_attribute(cbz::VertexAttributeType::ePosition,
                          cbz::VertexFormat::eFloat32x3);
    layout.push_attribute(cbz::VertexAttributeType::eNormal,
                          cbz::VertexFormat::eFloat32x3);
    layout.push_attribute(cbz::VertexAttributeType::eTexCoord0,
                          cbz::VertexFormat::eFloat32x2);
    layout.end();

    // Create full screen quad vertex and index buffers
    mQuadVBH = cbz::VertexBufferCreate(
        layout, static_cast<uint32_t>(sQuadVertices.size()),
        sQuadVertices.data());
    mQuadIBH = cbz::IndexBufferCreate(
        cbz::IndexFormat::eUint16, static_cast<uint32_t>(sQuadIndices.size()),
        sQuadIndices.data());

    // Create blit texture
    mBlitTH = cbz::Texture2DCreate(cbz::TextureFormat::eRGBA8Unorm, kWidth,
                                   kHeight, "blitTexture");

    // --- Voxel Ray Tracing Setup ---
    // Create raytracing compute program
    mRaytracingSH = cbz::ShaderCreate("assets/shaders/voxel_raytracing.slang",
                                      "raytracing_shader");
    mRaytracingPH = cbz::ComputeProgramCreate(mRaytracingSH);

    // Create ray tracing uniforms
    mRayTracingUH =
        cbz::UniformCreate("uRaytracingSettings", cbz::UniformType::eVec4);
    mCameraUH = cbz::UniformCreate("uCamera", cbz::UniformType::eVec4, 2);
  }

  void update() {
    mLastTime = mTime;
    mTime = glfwGetTime();
    mDeltaTime = mTime - mLastTime;

    struct {
      uint32_t dim[4] = {kWidth, kHeight, 0, 0};
    } raytracingSettings;
    raytracingSettings.dim[2] = mFrameCtr;

    float movementSpeed = 5;
    static struct {
      float settings[4] = {1.0f, 0.0f, 0.0f, 0.0f}; // focal length
      glm::vec4 position;                           // x, y, z, _padding
    } camera;

    if (cbz::IsKeyDown(cbz::Key::eW)) {
      camera.position.z -= mDeltaTime * movementSpeed;
    }

    if (cbz::IsKeyDown(cbz::Key::eS)) {
      camera.position.z += mDeltaTime * movementSpeed;
    }

    if (cbz::IsKeyDown(cbz::Key::eD)) {
      camera.position.x += mDeltaTime * movementSpeed;
    }

    if (cbz::IsKeyDown(cbz::Key::eA)) {
      camera.position.x -= mDeltaTime * movementSpeed;
    }

    if (cbz::IsKeyDown(cbz::Key::eSpace)) {
      camera.position.y += mDeltaTime * movementSpeed;
    }

    if (cbz::IsKeyDown(cbz::Key::eLeftShift)) {
      camera.position.y -= mDeltaTime * movementSpeed;
    }

    // --- Compute pass ---
    // Set 'mImageSBH' structured buffer to populate
    cbz::StructuredBufferSet(cbz::BufferSlot::e0, mImageSBH);

    // Set Raytracing uniforms
    cbz::UniformSet(mCameraUH, &camera);
    cbz::UniformSet(mRayTracingUH, &raytracingSettings);

    // Submit to compute target
    Submit(0, mRaytracingPH, kWidth + 7 / 8, kWidth + 7 / 8, 1);

    // --- Blit pass ---
    // Set populated mImageSBH from compute pass
    cbz::StructuredBufferSet(cbz::BufferSlot::e1, mImageSBH);

    // Set quad vertex and index buffers
    cbz::VertexBufferSet(mQuadVBH);
    cbz::IndexBufferSet(mQuadIBH);

    // Set graphics transform to identity
    glm::mat4 transform = glm::mat4(1.0f);
    transform = glm::transpose(transform);
    cbz::TransformSet(glm::value_ptr(transform));

    // Submit to graphics target
    cbz::Submit(1, mBlitPH);

    mFrameCtr++;
    cbz::Frame();
  }

  void shutdown() {
    // Destroy common resources
    cbz::TextureDestroy(mBlitTH);

    // Destroy compute resources
    cbz::ShaderDestroy(mRaytracingSH);
    cbz::ComputeProgramDestroy(mRaytracingPH);
    cbz::UniformDestroy(mCameraUH);
    cbz::UniformDestroy(mRayTracingUH);
    cbz::StructuredBufferDestroy(mImageSBH);

    // Destroy blit resources
    cbz::VertexBufferDestroy(mQuadVBH);
    cbz::IndexBufferDestroy(mQuadIBH);
    cbz::GraphicsProgramDestroy(mBlitPH);
    cbz::ShaderDestroy(mBlitSH);

    cbz::Shutdown();
  }

private:
  // Common raytraced image buffer
  cbz::StructuredBufferHandle mImageSBH;

  // Blit resources
  cbz::ShaderHandle mBlitSH;
  cbz::GraphicsProgramHandle mBlitPH;
  cbz::VertexBufferHandle mQuadVBH;
  cbz::IndexBufferHandle mQuadIBH;
  cbz::TextureHandle mBlitTH;

  // Compute resources
  cbz::UniformHandle mRayTracingUH;
  cbz::UniformHandle mCameraUH;
  cbz::ShaderHandle mRaytracingSH;
  cbz::ComputeProgramHandle mRaytracingPH;

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
