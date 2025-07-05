#ifndef CBZ_MEMORY_H_
#define CBZ_MEMORY_H_

namespace cbz {

template <typename T> using Ref = std::shared_ptr<T>;

template <typename T, typename... Args> Ref<T> RefCreate(Args &&...args) {
  return std::make_shared<T>(std::forward<Args>(args)...);
};

CBZ_API struct Buffer {
  Buffer(void *data, size_t size);
  virtual ~Buffer();

  inline const uint8_t *getData() const { return mData; }
  inline uint32_t getSize() const { return mSize; }

private:
  uint8_t *mData;
  uint32_t mSize;
};

}; // namespace cbz

#endif
