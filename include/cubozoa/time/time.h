#ifndef CBZ_TIME_H_
#define CBZ_TIME_H_

#include "cubozoa/cubozoa_defines.h"
#include <functional>

namespace cbz {

CBZ_API class ScopedTimer {
public:
  ScopedTimer(std::function<void(double delta)> fn)
      : mStart(std::chrono::high_resolution_clock::now()), mFn(fn) {}

  ~ScopedTimer() {
    auto diff = std::chrono::high_resolution_clock::now() - mStart;
    double seconds = std::chrono::duration<double>(diff).count();

    if (mFn) {
      mFn(seconds);
    }
  }

private:
  std::chrono::high_resolution_clock::time_point mStart;
  std::function<void(double delta)> mFn;
};

}; // namespace cbz

#endif
