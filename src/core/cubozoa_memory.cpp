#include "cubozoa/memory/cubozoa_memory.h"

namespace cbz {

Buffer::Buffer(void *data, size_t size) : mData(static_cast<uint8_t *>(data)), mSize(static_cast<uint32_t>(size)) {
  if (size > std::numeric_limits<uint32_t>::max()) {
    spdlog::error("Content length too large! Exceeds {} bytes!",
                  std::numeric_limits<uint32_t>::max());
    return;
  }
}

Buffer::~Buffer() {
  if (mData != nullptr) {
    delete mData;
  }
}

} // namespace cbz
