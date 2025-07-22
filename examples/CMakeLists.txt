add_executable(cubes cubes.cpp)
target_link_libraries(cubes PRIVATE cubozoa)
if (EMSCRIPTEN)
    set_target_properties(cubes PROPERTIES SUFFIX ".html")
    target_link_options(cubes PRIVATE "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets")
else()
    target_copy_webgpu_binaries(cubes)
endif()

file(GLOB_RECURSE ASSET_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/*"
)

add_executable(raytracing raytracing.cpp)
target_link_libraries(raytracing PRIVATE cubozoa)
if (EMSCRIPTEN)
    set_target_properties(raytracing PROPERTIES SUFFIX ".html")
    target_link_options(raytracing PRIVATE "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets")
else()
    target_copy_webgpu_binaries(raytracing)
endif()

add_executable(voxel_raytracing voxel_raytracing.cpp)
target_link_libraries(voxel_raytracing PRIVATE cubozoa)
if (EMSCRIPTEN)
    set_target_properties(voxel_raytracing PROPERTIES SUFFIX ".html")
    target_link_options(voxel_raytracing  PRIVATE "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets")
else()
    target_copy_webgpu_binaries(voxel_raytracing)
endif()

# Set your shader source and output directories
set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders)
set(SHADER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

# Find all .slang files recursively
file(GLOB_RECURSE SLANG_SHADERS RELATIVE ${SHADER_SRC_DIR} ${SHADER_SRC_DIR}/*.slang)
list(REMOVE_ITEM SLANG_SHADERS "cbz.slang")

# Collect output targets to build as dependencies
set(SLANG_OUTPUTS)

foreach(SHADER_FILE ${SLANG_SHADERS})
    # Full path to the input shader
    set(INPUT_PATH ${SHADER_SRC_DIR}/${SHADER_FILE})

    # Replace extension with .spirv and .json
    string(REPLACE ".slang" ".spirv" SPIRV_FILE ${SHADER_FILE})
    string(REPLACE ".slang" ".json"  JSON_FILE  ${SHADER_FILE})

    # Output paths in the working directory
    set(SPIRV_OUTPUT ${SHADER_OUT_DIR}/${SPIRV_FILE})
    set(JSON_OUTPUT  ${SHADER_OUT_DIR}/${JSON_FILE})

    # Make sure the output directories exist
    get_filename_component(SPIRV_DIR ${SPIRV_OUTPUT} DIRECTORY)
    file(MAKE_DIRECTORY ${SPIRV_DIR})


    add_custom_command(
        OUTPUT ${SPIRV_OUTPUT} ${JSON_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPIRV_DIR}
        COMMAND slangc ${INPUT_PATH}
                -target spirv
                -o ${SPIRV_OUTPUT}
                -reflection-json ${JSON_OUTPUT}
        DEPENDS ${INPUT_PATH}
        COMMENT "Compiling shader ${SHADER_FILE}"
        VERBATIM
    )

    list(APPEND SLANG_OUTPUTS ${SPIRV_OUTPUT} ${JSON_OUTPUT})
endforeach()

# Add a target that builds all shaders
add_custom_target(compile_shaders ALL DEPENDS ${SLANG_OUTPUTS})
