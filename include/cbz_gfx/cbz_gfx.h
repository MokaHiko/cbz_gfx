#ifndef CBZ_H_
#define CBZ_H_

#include "cbz_gfx_defines.h"

#include <cbz/cbz_defines.h>

#include <functional>

namespace cbz {

struct CBZ_API InitDesc {
  const char *name;
  uint32_t width;
  uint32_t height;
  CBZNetworkStatus netStatus;
};

CBZ_API Result Init(InitDesc initDesc);

CBZ_API VertexBufferHandle VertexBufferCreate(const VertexLayout &vertexLayout,
                                              uint32_t vertexCount,
                                              const void *data = nullptr,
                                              const char *name = "");

CBZ_API void VertexBufferSet(VertexBufferHandle vbh);

CBZ_API void VertexBufferDestroy(VertexBufferHandle vbh);

[[nodiscard]] CBZ_API IndexBufferHandle
IndexBufferCreate(CBZIndexFormat format, uint32_t num,
                  const void *data = nullptr, const char *name = "");

CBZ_API void IndexBufferSet(IndexBufferHandle ibh);

CBZ_API void IndexBufferDestroy(IndexBufferHandle ibh);

[[nodiscard]] CBZ_API StructuredBufferHandle StructuredBufferCreate(
    CBZUniformType type, uint32_t elementCount,
    const void *elementData = nullptr, int flags = 0, const char *name = "");

CBZ_API void StructuredBufferUpdate(StructuredBufferHandle sbh,
                                    uint32_t elementCount, const void *data,
                                    uint32_t offset = 0);

CBZ_API void StructuredBufferSet(CBZBufferSlot slot, StructuredBufferHandle sbh,
                                 CBZBool32 dynamic = false);

CBZ_API void StructuredBufferDestroy(StructuredBufferHandle ibh);

/// @brief Creates a uniform.
/// @note The uniform name must match the name used in the shader exactly.
/// mapping).
[[nodiscard]] CBZ_API UniformHandle UniformCreate(const char *name,
                                                  CBZUniformType type,
                                                  uint16_t num = 1);

/// @brief Updates a uniform.
/// @note If buffer and num are 0, the entire uniform range is updated.
CBZ_API void UniformSet(UniformHandle uh, const void *data, uint16_t num = 0);

/// @brief Destroys a uniform.
CBZ_API void UniformDestroy(UniformHandle uh);

[[nodiscard]] CBZ_API ImageHandle Image2DCreate(CBZTextureFormat format,
                                                uint32_t w, uint32_t h,
                                                int flags = 0);

[[nodiscard]] CBZ_API ImageHandle Image2DCubeMapCreate(CBZTextureFormat format,
                                                       uint32_t w, uint32_t h,
                                                       uint32_t depth,
                                                       int flags = 0);

CBZ_API void ImageSetName(ImageHandle imgh, const char *name, uint32_t len);

CBZ_API void Image2DUpdate(ImageHandle imgh, void *data, uint32_t count);

CBZ_API void TextureSet(CBZTextureSlot slot, ImageHandle imgh,
                        TextureBindingDesc desc = {});

CBZ_API void ImageDestroy(ImageHandle imgh);

CBZ_NO_DISCARD CBZ_API ShaderHandle ShaderCreate(const char *path,
                                                 int flags = 0);

CBZ_API void ShaderSetName(ShaderHandle sh, const char *name, uint32_t len);

CBZ_API void ShaderDestroy(ShaderHandle sh);

CBZ_NO_DISCARD CBZ_API GraphicsProgramHandle
GraphicsProgramCreate(ShaderHandle sh, int flags = 0);

CBZ_API void GraphicsProgramSetName(GraphicsProgramHandle gph, const char *name,
                                    uint32_t len);

CBZ_API void GraphicsProgramDestroy(GraphicsProgramHandle gph);

CBZ_API ComputeProgramHandle ComputeProgramCreate(ShaderHandle sh,
                                                  const char *name = "");

CBZ_API void ComputeProgramDestroy(ComputeProgramHandle gph);

CBZ_API void TransformSet(const float *transform);

CBZ_API void ProjectionSet(const float *projection);

CBZ_API void ViewSet(const float *projection);

CBZ_API void ReadBufferAsync(StructuredBufferHandle sbh,
                             std::function<void(const void *data)> callback);

// [[deprecated("not really, just incomplete :)")]]
CBZ_API void TextureReadAsync(ImageHandle imgh, const Origin3D *origin,
                              const TextureExtent *extent,
                              std::function<void(const void *data)> callback);

CBZ_API void
RenderTargetSet(uint8_t target, const AttachmentDescription *colorAttachments,
                uint32_t colorAttachmentCount,
                const AttachmentDescription *depthAttachment = NULL);

/// @brief Submits a graphics program for rendering on the given target.
///
/// @param target An ID representing the output/render target.
/// @param gph The graphics program handle to be submitted for rendering.
///
/// @note Submissions within the same target are not guaranteed to preserve
/// submission order.
///       Sorting is typically handled via `sortKey` or other pipeline criteria.
CBZ_API void Submit(uint8_t target, GraphicsProgramHandle gph);

/// @brief Submits a compute program for dispatch on the given target.
///
/// @param target An ID representing the compute dispatch group or context.
/// @param cph The compute program handle to be submitted for execution.
/// @param x The number of workgroups to dispatch in the X dimension.
/// @param y The number of workgroups to dispatch in the Y dimension.
/// @param z The number of workgroups to dispatch in the Z dimension.
///
/// @note Submissions within the same target are not guaranteed to preserve
/// submission order.
///       Ensure synchronization if execution order is important.
CBZ_API void Submit(uint8_t target, ComputeProgramHandle cph, uint32_t x,
                    uint32_t y, uint32_t z);

// @returns the frame number.
CBZ_API uint32_t Frame();

CBZ_API void Shutdown();

enum class Key {
  eUnknown = -1,

  eSpace = 32,
  eApostrophe = 39,
  eComma = 44,
  eMinus = 45,
  ePeriod = 46,
  eSlash = 47,
  e0 = 48,
  e1 = 49,
  e2 = 50,
  e3 = 51,
  e4 = 52,
  e5 = 53,
  e6 = 54,
  e7 = 55,
  e8 = 56,
  e9 = 57,
  eSemicolon = 59 /* ; */,
  eEqual = 61 /* = */,
  eA = 65,
  eB = 66,
  eC = 67,
  eD = 68,
  eE = 69,
  eF = 70,
  eG = 71,
  eH = 72,
  eI = 73,
  eJ = 74,
  eK = 75,
  eL = 76,
  eM = 77,
  eN = 78,
  eO = 79,
  eP = 80,
  eQ = 81,
  eR = 82,
  eS = 83,
  eT = 84,
  eU = 85,
  eV = 86,
  eW = 87,
  eX = 88,
  eY = 89,
  eZ = 90,
  eLeftBracket = 91,
  eBackSlash = 92,
  eRightBbracket = 93,
  eGraveAccent = 96,
  eWorld1 = 161 /* non-us #1 */,
  eWorld2 = 162 /* non-us #2 */,

  /* function keys */
  eEscape = 256,
  eEnter = 257,
  eTab = 258,
  eBackspace = 259,
  eInsert = 260,
  eDelete = 261,
  eRight = 262,
  eLeft = 263,
  eDown = 264,
  eUp = 265,
  ePageUp = 266,
  ePageDown = 267,
  eHome = 268,
  eEnd = 269,
  eCaps_lock = 280,
  eScroll_lock = 281,
  eNum_lock = 282,
  ePrint_screen = 283,
  ePause = 284,
  eF1 = 290,
  eF2 = 291,
  eF3 = 292,
  eF4 = 293,
  eF5 = 294,
  eF6 = 295,
  eF7 = 296,
  eF8 = 297,
  eF9 = 298,
  eF10 = 299,
  eF11 = 300,
  eF12 = 301,
  eF13 = 302,
  eF14 = 303,
  eF15 = 304,
  eF16 = 305,
  eF17 = 306,
  eF18 = 307,
  eF19 = 308,
  eF20 = 309,
  eF21 = 310,
  eF22 = 311,
  eF23 = 312,
  eF24 = 313,
  eF25 = 314,
  eKp_0 = 320,
  eKp_1 = 321,
  eKp_2 = 322,
  eKp_3 = 323,
  eKp_4 = 324,
  eKp_5 = 325,
  eKp_6 = 326,
  eKp_7 = 327,
  eKp_8 = 328,
  eKp_9 = 329,
  eKpDecimal = 330,
  eKpDivide = 331,
  eKpMultiply = 332,
  eKpSubtract = 333,
  eKpAdd = 334,
  eKpEnter = 335,
  eKpEqual = 336,
  eLeftShift = 340,
  eLeftControl = 341,
  eLeftAlt = 342,
  eLeftSuper = 343,
  eRightShift = 344,
  eRightControl = 345,
  eRightAlt = 346,
  eRightSuper = 347,
  eMenu = 348,
  eCount,
};

enum class MouseButton : uint32_t {
  e1 = 0,
  e2 = 1,
  e3 = 2,
  e4 = 3,
  e5 = 4,
  e6 = 5,
  e7 = 6,
  e8 = 7,
  eCount,

  eLast = e8,
  eLeft = e1,
  eRight = e2,
  eMiddle = e3,
};

// Input functions
CBZ_NO_DISCARD CBZ_API CBZBool32 IsKeyDown(Key key);
CBZ_NO_DISCARD CBZ_API CBZBool32 IsKeyPressed(Key key);

struct CBZ_API MousePosition {
  uint32_t x, y;
};

[[nodiscard]] CBZ_API MousePosition GetMousePosition();
[[nodiscard]] CBZ_API CBZBool32 IsMouseButtonDown(MouseButton mouseButton);
[[nodiscard]] CBZ_API CBZBool32 IsMouseButtonPressed(MouseButton mouseButton);

}; // namespace cbz
//
namespace cbz::input {

enum class Axis {
  MouseX,
  MouseY,
};

[[nodiscard]] double GetAxis(Axis axis);

}; // namespace cbz::input

// TODO: Make C bindings
// --- C bindings ---
#ifdef __cplusplus
extern "C" { // Ensure C linkage compatibility
#endif

#ifdef __cplusplus
} // End extern "C"
#endif

#endif
