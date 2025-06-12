#include "cubozoa/cubozoa.h"

#include <webgpu/webgpu.h>

WGPUInstance sInstance = nullptr;

namespace cbz {
Result init() {
  WGPUInstanceDescriptor instanceDesc = {
      nullptr, // nextInChain
      {},      // label
  };

  sInstance = wgpuCreateInstance(&instanceDesc);
  return Result::Success;
}

void shutdown() { wgpuInstanceRelease(sInstance); }
} // namespace cbz
