#include "cubozoa_file.h"

#include <fstream>
#include <sstream>

namespace cbz {

Result LoadFileAsBinary(const std::string &filePath,
                        std::vector<uint8_t> &out) {
  std::ifstream file(filePath, std::ios::binary | std::ios::ate);

  if (!file) {
    spdlog::error("Failed to load file at {}", filePath);
    return Result::eFileError;
  }

  size_t len = file.tellg();
  out.resize(len);

  file.seekg(0);
  if (!file.read((char *)out.data(), len)) {
    file.close();

    spdlog::error("Failed to read file at {}", filePath);
    return Result::eFileError;
  }

  file.close();
  return Result::eSuccess;
}

Result LoadFileAsText(const std::string &filePath, std::string &out) {
  std::ifstream file(filePath); // text mode is default, no need for ios::binary
  if (!file.is_open()) {
    spdlog::error("Failed to read file at {}", filePath);
    return Result::eFailure;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  out = buffer.str();

  return Result::eSuccess;
}

} // namespace cbz
