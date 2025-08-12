#ifndef CBZ_ASSET_H_
#define CBZ_ASSET_H_

#include <cubozoa/cubozoa_defines.h>
#include <filesystem>

// An asset is backed by a path
template <typename ReferenceT> class Asset {
public:
  Asset(std::filesystem::path path)
      : mName(path.has_filename() ? path.filename().string() : path.string()),
        mPath(path) {};

  virtual ~Asset() = default;

  inline const std::filesystem::path getPath() const { return mPath; }
  inline const std::string &getName() const { return mName; }

  [[nodiscard]] virtual cbz::Result load() { return cbz::Result::eFailure; }

  [[nodiscard]] virtual ReferenceT makeRef() = 0;

private:
  std::string mName;
  std::filesystem::path mPath;
};

#endif // CBZ_ASSET_H_
