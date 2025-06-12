#ifndef CBZ_H_
#define CBZ_H_

namespace cbz {

enum class Result {
  Success = 0,
  Failure = 1,
};

[[nodiscard]] Result init();

void shutdown();

} // namespace cbz

#endif
