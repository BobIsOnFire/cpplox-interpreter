cmake_minimum_required(VERSION 3.30)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "0e5b6991-d74f-4b3d-a41c-cf096e0b2508")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_MODULE_STD 1)

project(modules CXX)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES Clang OR NOT CMAKE_SYSTEM_NAME STREQUAL Linux)
  message(FATAL_ERROR "This project can only be built on Linux with Clang.")
endif()

# Additional compile/link flags
add_compile_options(-g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-private-field -Wno-unused-function)
include(cmake/Sanitizers.cmake)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)

