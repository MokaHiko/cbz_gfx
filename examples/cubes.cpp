#include <cstdlib>

#include <cubozoa/cubozoa.h>

int main() {
  cbz::App app;

  if (app.init({"Cubes", 1280, 720}) != cbz::Result::Success) {
    return -1;
  }

  app.run();

  app.shutdown();
}
