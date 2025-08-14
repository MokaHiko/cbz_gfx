cmake_minimum_required(VERSION 3.0...3.25)
project(cbz_gfx VERSION 0.1.0 LANGUAGES CXX C)

option(CBZ_GFX_BUILD_EXAMPLES "Build examples" OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT EMSCRIPTEN)
    add_subdirectory(third_party/glfw)
    add_subdirectory(third_party/wgpu-native)

    set(ASIO_BUILD_EXAMPLES OFF CACHE INTERNAL "")
    set(ASIO_BUILD_TESTS OFF CACHE INTERNAL "")
    set(ASIO_BUILD_DOCS OFF CACHE INTERNAL "")
    FetchContent_Declare(
      asio
      GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
      GIT_TAG master
    )

    FetchContent_MakeAvailable(asio)
else()
    # Create a mock 'glfw' target that just sets the `-sUSE_GLFW=3` link option:
    add_library(glfw INTERFACE)
    target_link_options(glfw INTERFACE -sUSE_GLFW=3 -sASYNCIFY -sUSE_WEBGPU -sASSERTIONS -sALLOW_MEMORY_GROWTH)
endif()

add_subdirectory(third_party/glfw3webgpu)
add_subdirectory(third_party/imgui)
add_subdirectory(third_party/murmurhash)

# TODO: Allow shared (ImGui Issue)
if(CBZ_BUILD_SHARED)
	add_library(${PROJECT_NAME} STATIC
            src/net/cbz_net.cpp
            src/net/cbz_net_http.cpp

            src/cbz_irenderer_context.cpp
            src/cbz_renderer_webgpu.cpp

	        src/cbz_gfx_imgui.cpp
            src/cbz_gfx.cpp)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CBZ_EXPORTS)
else()
	add_library(${PROJECT_NAME}
            src/net/cbz_net.cpp
            src/net/cbz_net_http.cpp

            src/cbz_irenderer_context.cpp
            src/cbz_renderer_webgpu.cpp

	        src/cbz_gfx_imgui.cpp
            src/cbz_gfx.cpp)
endif()

# Set definitions per configuration
target_compile_definitions(${PROJECT_NAME} PUBLIC
    $<$<CONFIG:Debug>:CBZ_DEBUG>
    $<$<CONFIG:Release>:CBZ_RELEASE>
)

target_link_libraries(${PROJECT_NAME} PUBLIC cbz glm glfw3webgpu imgui murmurhash)

if(EMSCRIPTEN)
    target_compile_definitions(${PROJECT_NAME} PUBLIC WEBGPU_BACKEND_EMSCRIPTEN)
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE ASIO_STANDALONE)
    target_include_directories(${PROJECT_NAME} PRIVATE
      ${asio_SOURCE_DIR}/asio/include
    )

    target_compile_definitions(${PROJECT_NAME} PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_WGPU)
    target_compile_definitions(${PROJECT_NAME} PUBLIC WEBGPU_BACKEND_WGPU)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PUBLIC src)
target_include_directories(${PROJECT_NAME} PRIVATE third_party)
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    COMPILE_WARNING_AS_ERROR ON
)

if (MSVC)
    # target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic)
    target_link_options(${PROJECT_NAME} PRIVATE -fsanitize=address)
endif()

if (XCODE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        XCODE_GENERATE_SCHEME ON
        XCODE_SCHEME_ENABLE_GPU_FRAME_CAPTURE_MODE "Metal"
    )
endif()

if(CBZ_GFX_BUILD_EXAMPLES)
    # add_subdirectory(examples)
endif()
