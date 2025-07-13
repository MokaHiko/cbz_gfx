#ifndef CBZ_MEMORY_H_
#define CBZ_MEMORY_H_

#include <memory>

namespace cbz {

template <typename T> using Ref = std::shared_ptr<T>;
template <typename T> using Scope = std::unique_ptr<T>;

template <typename T, typename... Args> Ref<T> RefCreate(Args &&...args) {
  return std::make_shared<T>(std::forward<Args>(args)...);
};

template <typename T, typename... Args> Scope<T> ScopeCreate(Args &&...args) {
  return std::make_unique<T>(std::forward<Args>(args)...);
};

/// @brief Represents a block of raw memory data with size tracking.
CBZ_API struct Buffer {
  /// @brief Constructs a Buffer by taking ownership of raw data.
  /// @param data Pointer to heap-allocated memory (must be allocated with `new
  /// uint8_t[]`).
  /// @param size Number of bytes pointed to by `data`.
  Buffer(void *data, size_t size);

  /// @brief Destroys the Buffer and frees the owned memory.
  virtual ~Buffer();

  /// @brief Returns a pointer to the buffer's data.
  /// @return A constant pointer to the internal byte data.
  inline const uint8_t *getData() const { return mData; }

  /// @brief Returns the size of the buffer in bytes.
  /// @return Number of bytes stored in the buffer.
  inline uint32_t getSize() const { return mSize; }

private:
  uint8_t *mData; ///< Pointer to raw memory owned by the buffer.
  uint32_t mSize; ///< Size of the buffer in bytes.
};

}; // namespace cbz

#endif
