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
    # Add the 'glfw' directory, which contains the definition of a 'glfw' target
    add_subdirectory(third_party/glfw)
    add_subdirectory(third_party/wgpu-native)
else()
    # Create a mock 'glfw' target that just sets the `-sUSE_GLFW=3` link option:
    add_library(glfw INTERFACE)
    target_link_options(glfw INTERFACE -sUSE_GLFW=3)
endif()
add_subdirectory(third_party/glfw3webgpu)

add_library(${PROJECT_NAME} src/core/cbz_file.cpp src/cubuzoa.cpp)
target_link_libraries(${PROJECT_NAME} PUBLIC spdlog glfw webgpu glfw3webgpu)
target_include_directories(${PROJECT_NAME} PUBLIC include)
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

add_subdirectory(examples)
