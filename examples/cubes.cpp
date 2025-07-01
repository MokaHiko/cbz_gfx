#include <cubozoa/cubozoa.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

std::vector<float> vertices = {
    // x,   y,  r,   g,   b
    -0.5, -0.5, 1.0, 0.0, 0.0, // 0
    +0.5, -0.5, 0.0, 1.0, 0.0, // 1
    +0.5, +0.5, 0.0, 0.0, 1.0, // 2
    -0.5, +0.5, 1.0, 1.0, 0.0  // 3
};

std::vector<uint16_t> indices = {
    0, 1, 2, // Triangle #0 connects points #0, #1 and #2
    0, 2, 3  // Triangle #1 connects points #0, #2 and #3
};

class CubesApp {
public:
  void init() {
    if (cbz::init({"Cubes", 1280, 720}) != cbz::Result::eSuccess) {
    }

    mTriangleSH = cbz::shaderCreate("assets/shaders/triangle.wgsl");
    mTrianglePH = cbz::graphicsProgramCreate(mTriangleSH);

    cbz::VertexLayout layout;
    layout.begin(cbz::VertexStepMode::eVertex);
    layout.push_attribute(cbz::VertexAttributeType::ePosition,
                          cbz::VertexFormat::eFloat32x2);
    layout.push_attribute(cbz::VertexAttributeType::eColor,
                          cbz::VertexFormat::eFloat32x3);
    layout.end();

    mTriangleVBH = cbz::vertexBufferCreate(
        layout, static_cast<uint32_t>(vertices.size()), vertices.data());

    mTriangleIBH = cbz::indexBufferCreate(cbz::IndexFormat::eUint16,
                                          static_cast<uint32_t>(indices.size()),
                                          indices.data());
  }

  void update() {
    glm::mat4 identity{1.0f};
    cbz::transformBind(glm::value_ptr(identity));

    cbz::vertexBufferBind(mTriangleVBH);
    cbz::indexBufferBind(mTriangleIBH);

    cbz::submit(0, mTrianglePH);
    cbz::frame();
  }

  void shutdown() {
    cbz::shaderDestroy(mTriangleSH);
    cbz::graphicsProgramDestroy(mTrianglePH);
    cbz::vertexBufferDestroy(mTriangleVBH);
    cbz::indexBufferDestroy(mTriangleIBH);

    cbz::shutdown();
  }

private:
  cbz::ShaderHandle mTriangleSH;
  cbz::GraphicsProgramHandle mTrianglePH;
  cbz::VertexBufferHandle mTriangleVBH;
  cbz::IndexBufferHandle mTriangleIBH;
};

int main(int, char **) {
  CubesApp app = {};
  app.init();

#ifndef __EMSCRIPTEN__
  while (true) {
    app.update();
  }
#else
  emscripten_set_main_loop_arg(
      [](void *userData) {
        static int i = 0;
        CubesApp *app = reinterpret_cast<CubesApp *>(userData);
        app->update();
      },
      (void *)&app, // value sent to the 'userData' arg of the callback
      0, true);
#endif

  app.shutdown();
}
