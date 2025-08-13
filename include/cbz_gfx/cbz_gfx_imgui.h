#ifndef CBZ_IMGUI_H_
#define CBZ_IMGUI_H_

#include <cbz_gfx/cbz_gfx_defines.h>
#include <imgui.h>

namespace cbz::imgui {

CBZ_API void Image(cbz::ImageHandle imgh, const ImVec2 &size,
                   const ImVec2 &uv0 = ImVec2(0, 0),
                   const ImVec2 &uv1 = ImVec2(1, 1),
                   const ImVec4 &tint_col = ImVec4(1, 1, 1, 1),
                   const ImVec4 &border_col = ImVec4(0, 0, 0, 0));

}; // namespace cbz::imgui

typedef void (*CBZ_ImGuiRenderFunc)();
namespace cbz {

// Registers a callback that will be invoked during the engine's ImGui render
// phase. Pass nullptr to disable. The callback should only contain ImGui widget
// code.
CBZ_API void SetImGuiRenderCallback(CBZ_ImGuiRenderFunc func);

} // namespace cbz

#endif
