cmake_minimum_required(VERSION 3.0...3.25)
project(cubozoa VERSION 0.1.0 LANGUAGES CXX C)

include(FetchContent)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.15.3
)
FetchContent_MakeAvailable(spdlog)

if (NOT EMSCRIPTEN)
    add_subdirectory(third_party/glfw)
    add_subdirectory(third_party/wgpu-native)
else()
    # Create a mock 'glfw' target that just sets the `-sUSE_GLFW=3` link option:
    add_library(glfw INTERFACE)
    target_link_options(glfw INTERFACE -sUSE_GLFW=3 -sASYNCIFY -sUSE_WEBGPU -sASSERTIONS -sALLOW_MEMORY_GROWTH)
endif()
add_subdirectory(third_party/glfw3webgpu)
add_subdirectory(third_party/imgui)
add_subdirectory(third_party/glm)
add_subdirectory(third_party/murmurhash)

add_library(asio INTERFACE)
target_include_directories(asio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/asio/asio/include)
target_compile_definitions(asio INTERFACE ASIO_STANDALONE)

add_library(${PROJECT_NAME}
            src/core/cubozoa_memory.cpp

            src/core/cubozoa_file.cpp

            src/math/cubozoa_math.cpp

            src/renderer/cubozoa_irenderer_context.cpp
            src/renderer/cubozoa_renderer_webgpu.cpp

            src/net/cubozoa_net.cpp
            src/net/cubozoa_net_http.cpp

            src/cubozoa.cpp)

target_link_libraries(${PROJECT_NAME} PUBLIC spdlog glfw glm webgpu glfw3webgpu imgui murmurhash asio)

if(EMSCRIPTEN)
    target_compile_definitions(${PROJECT_NAME} PUBLIC WEBGPU_BACKEND_EMSCRIPTEN)
else()

    target_compile_definitions(${PROJECT_NAME} PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_WGPU)

endif()

target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PUBLIC src)
target_precompile_headers(${PROJECT_NAME} PUBLIC src/cbz_pch.h)
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    COMPILE_WARNING_AS_ERROR ON
)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

if (XCODE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        XCODE_GENERATE_SCHEME ON
        XCODE_SCHEME_ENABLE_GPU_FRAME_CAPTURE_MODE "Metal"
    )
endif()

target_compile_definitions(${PROJECT_NAME} PUBLIC
    CBZ_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/"
)

add_subdirectory(examples)
