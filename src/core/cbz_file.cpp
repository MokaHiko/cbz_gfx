#include "cbz_file.h"

#include <cstdint>
#include <fstream>
#include <ios>

namespace cbz {

Result loadFileFromPath(const std::string &filePath,
                        std::vector<uint8_t> &out) {
  std::ifstream file(filePath, std::ios::binary | std::ios::ate);

  if (!file) {
    spdlog::error("Failed to load file at {}", filePath);
    return Result::FileError;
  }

  size_t len = file.tellg();
  out.resize(len);

  file.seekg(0);
  if (!file.read((char *)out.data(), len)) {
    spdlog::error("Failed to read file at {}", filePath);
    return Result::FileError;
  }

  file.close();

  return Result::Success;
}

} // namespace cbz
