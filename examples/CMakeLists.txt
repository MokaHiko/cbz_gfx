if (XCODE)
    set_target_properties(App PROPERTIES
        XCODE_GENERATE_SCHEME ON
        XCODE_SCHEME_ENABLE_GPU_FRAME_CAPTURE_MODE "Metal"
    )
endif()

add_executable(cubes cubes.cpp)
target_link_libraries(cubes PRIVATE cubozoa)
target_copy_webgpu_binaries(cubes)

if (EMSCRIPTEN)
    set_target_properties(cubes PROPERTIES SUFFIX ".html")
    target_link_options(cubes PRIVATE -sASYNCIFY)
endif()
