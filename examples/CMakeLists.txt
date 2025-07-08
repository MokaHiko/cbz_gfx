add_executable(cubes cubes.cpp)
target_link_libraries(cubes PRIVATE cubozoa)
if (EMSCRIPTEN)
    set_target_properties(cubes PROPERTIES SUFFIX ".html")
    target_link_options(cubes PRIVATE "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets")
else()
    # target_copy_webgpu_binaries(cubes)
    # add_custom_command(TARGET cubes POST_BUILD
    #     COMMAND ${CMAKE_COMMAND} -E echo "Copying assets directory to output..."
    #     COMMAND ${CMAKE_COMMAND} -E copy_directory
    #         "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    #         "$<TARGET_FILE_DIR:cubes>/assets"
    # )
endif()

# add_executable(gltf_viewer gltf_viewer.cpp)
# target_link_libraries(gltf_viewer PRIVATE cubozoa)
# if (EMSCRIPTEN)
#     set_target_properties(gltf_viewer PROPERTIES SUFFIX ".html")
#     target_link_options(gltf_viewer PRIVATE "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets")
# endif()

add_executable(raytracing raytracing.cpp)
target_link_libraries(raytracing PRIVATE cubozoa)
if (EMSCRIPTEN)
    set_target_properties(raytracing PROPERTIES SUFFIX ".html")
    target_link_options(raytracing PRIVATE "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets")
else()
    target_copy_webgpu_binaries(raytracing)
    add_custom_command(TARGET raytracing POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Copying assets directory to output..."
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:raytracing>/assets"
    )
endif()
