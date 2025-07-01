cmake_minimum_required(VERSION 3.14)

if (MSVC)
    # target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    # target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

project(slang-json VERSION 0.1.0 LANGUAGES CXX)

# Path to slang include and lib
set(SLANG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/slang)
set(SLANG_INCLUDE_DIR ${SLANG_DIR}/include)
set(SLANG_LIB_DIR ${SLANG_DIR}/lib-macos)

add_executable(${PROJECT_NAME}
    src/main.cpp
)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic)
endif()


set_target_properties(${PROJECT_NAME} PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  COMPILE_WARNING_AS_ERROR ON
)

target_include_directories(${PROJECT_NAME} PUBLIC ${SLANG_INCLUDE_DIR} third_party)

target_link_directories(${PROJECT_NAME} PUBLIC ${SLANG_LIB_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC slang)
