cmake_minimum_required(VERSION 3.0...3.25)
project(cubozoa VERSION 0.1.0 LANGUAGES CXX)

if (NOT EMSCRIPTEN)
    # Add the 'glfw' directory, which contains the definition of a 'glfw' target
    add_subdirectory(third_party/glfw)
    add_subdirectory(third_party/webgpu_dawn)
else()
    # Create a mock 'glfw' target that just sets the `-sUSE_GLFW=3` link option:
    add_library(glfw INTERFACE)
    target_link_options(glfw INTERFACE -sUSE_GLFW=3)
endif()

add_library(${PROJECT_NAME} src/cubuzoa.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE webgpu)
target_include_directories(${PROJECT_NAME} PUBLIC include)
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
endif()

add_subdirectory(examples)
