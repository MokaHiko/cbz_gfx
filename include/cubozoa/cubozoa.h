#ifndef CBZ_H_
#define CBZ_H_

#include "cubozoa_defines.h"

namespace cbz {

CBZ_API struct InitDesc {
  const char *name;
  uint32_t width;
  uint32_t height;
};

CBZ_API Result init(InitDesc initDesc);

[[nodiscard]] CBZ_API VertexBufferHandle
vertexBufferCreate(const VertexLayout &vertexLayout, uint32_t count,
                   const void *data = nullptr, const std::string &name = "");

CBZ_API void vertexBufferDestroy(VertexBufferHandle vbh);

[[nodiscard]] CBZ_API IndexBufferHandle
indexBufferCreate(IndexFormat format, uint32_t count,
                  const void *data = nullptr, const std::string &name = "");

CBZ_API void indexBufferDestroy(IndexBufferHandle ibh);

[[nodiscard]] CBZ_API UniformHandle uniformCreate(const std::string &name,
                                                  UniformType type,
                                                  uint16_t num = 1);

CBZ_API void uniformBind(UniformHandle uh, void *data, uint16_t num = 1);

CBZ_API void uniformDestroy(UniformHandle uh);

[[nodiscard]] CBZ_API TextureHandle texture2DCreate(
    TextureFormat format, uint32_t w, uint32_t h, const std::string &name = "");

CBZ_API void texture2DUpdate(TextureHandle th, void *data, uint32_t count);

CBZ_API void textureBind(uint16_t slot, TextureHandle th,
                         UniformHandle samplerUH, TextureBindingDesc desc = {});

void textureDestroy(TextureHandle th);

[[nodiscard]] CBZ_API ShaderHandle shaderCreate(const std::string &moduleName,
                                                const std::string &name = "");

CBZ_API void shaderDestroy(ShaderHandle sh);

[[nodiscard]] CBZ_API GraphicsProgramHandle
graphicsProgramCreate(ShaderHandle sh, const std::string &name = "");

CBZ_API void graphicsProgramDestroy(GraphicsProgramHandle gph);

CBZ_API void transformBind(float *transform);

CBZ_API void vertexBufferBind(VertexBufferHandle vbh);

CBZ_API void indexBufferBind(IndexBufferHandle ibh);

CBZ_API void submit(uint8_t target, GraphicsProgramHandle gph);

CBZ_API bool frame();

CBZ_API void shutdown();

}; // namespace cbz

#endif
