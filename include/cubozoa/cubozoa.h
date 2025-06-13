#ifndef CBZ_H_
#define CBZ_H_

namespace cbz {

struct InitDesc {
  const char *name;
  uint32_t width;
  uint32_t height;
};

CBZ_API class App {
public:
  App() = default;
  App(App &&) = delete;
  App &operator=(App &&) = delete;

  [[nodiscard]] Result init(InitDesc initDesc);

  void run();

  void shutdown();
};

} // namespace cbz

#endif
