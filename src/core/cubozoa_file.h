#ifndef CBZ_FILE_H_
#define CBZ_FILE_H_

#include <cubozoa/cubozoa_defines.h>

namespace cbz {

// @brief populates 'out' buffer with contents of file in 'filePath'
// @returns Result::Success(0) if succeseful.
[[nodiscard]] Result LoadFileAsBinary(const std::string &filePath,
                                      std::vector<uint8_t> &out);

// @brief populates 'out' buffer with contents of file in 'filePath'
// @returns Result::Success(0) if succeseful.
[[nodiscard]] Result LoadFileAsText(const std::string &filePath,
                                    std::string &out);

}; // namespace cbz

#endif
