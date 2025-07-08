#ifndef CBZ_H_
#define CBZ_H_

#include "cubozoa_defines.h"

namespace cbz {

CBZ_API struct InitDesc {
  const char *name;
  uint32_t width;
  uint32_t height;

  NetworkStatus netStatus;
};

CBZ_API Result init(InitDesc initDesc);

[[nodiscard]] CBZ_API VertexBufferHandle
VertexBufferCreate(const VertexLayout &vertexLayout, uint32_t num,
                   const void *data = nullptr, const std::string &name = "");

CBZ_API void VertexBufferSet(VertexBufferHandle vbh);

CBZ_API void VertexBufferDestroy(VertexBufferHandle vbh);

[[nodiscard]] CBZ_API IndexBufferHandle
IndexBufferCreate(IndexFormat format, uint32_t num, const void *data = nullptr,
                  const std::string &name = "");

CBZ_API void IndexBufferSet(IndexBufferHandle ibh);

CBZ_API void IndexBufferDestroy(IndexBufferHandle ibh);

[[nodiscard]] CBZ_API StructuredBufferHandle
StructuredBufferCreate(UniformType type, uint32_t num,
                       const void *data = nullptr, const std::string &name = "");

CBZ_API void StructuredBufferSet(BufferSlot slot, StructuredBufferHandle sbh);

CBZ_API void StructuredBufferDestroy(StructuredBufferHandle ibh);

/// @brief Creates a uniform.
/// @note The uniform name must match the name used in the shader exactly.
/// mapping).
[[nodiscard]] CBZ_API UniformHandle UniformCreate(const std::string &name,
                                                  UniformType type,
                                                  uint16_t num = 1);

/// @brief Updates a uniform.
/// @note If buffer and num are 0, the entire uniform range is updated.
CBZ_API void UniformSet(UniformHandle uh, void *data, uint16_t num = 0);

/// @brief Destroys a uniform.
CBZ_API void UniformDestroy(UniformHandle uh);

[[nodiscard]] CBZ_API TextureHandle Texture2DCreate(
    TextureFormat format, uint32_t w, uint32_t h, const std::string &name = "");

CBZ_API void Texture2DUpdate(TextureHandle th, void *data, uint32_t count);

CBZ_API void TextureSet(TextureSlot slot, TextureHandle th,
                        TextureBindingDesc desc = {});

void TextureDestroy(TextureHandle th);

[[nodiscard]] CBZ_API ShaderHandle ShaderCreate(const std::string &path,
                                                const std::string &name = "");

CBZ_API void ShaderDestroy(ShaderHandle sh);

[[nodiscard]] CBZ_API GraphicsProgramHandle
GraphicsProgramCreate(ShaderHandle sh, const std::string &name = "");

CBZ_API void GraphicsProgramDestroy(GraphicsProgramHandle gph);

[[nodiscard]] CBZ_API ComputeProgramHandle
ComputeProgramCreate(ShaderHandle sh, const std::string &name = "");

CBZ_API void ComputeProgramDestroy(ComputeProgramHandle gph);

CBZ_API void TransformSet(float *transform);

CBZ_API void Submit(uint8_t target, GraphicsProgramHandle gph);

CBZ_API bool Frame();

CBZ_API void Shutdown();

}; // namespace cbz

#endif
