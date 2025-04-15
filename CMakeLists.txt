cmake_minimum_required(VERSION 3.30)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "a9e1cf81-9932-4810-974b-6eccaf14e457")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_MODULE_STD 1)

project(modules CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL Linux)
  message(FATAL_ERROR "This project can only be built on Linux.")
endif()

# Additional compile/link flags
add_compile_options(-g)
include(cmake/ClangWarnings.cmake)
include(cmake/Sanitizers.cmake)

add_subdirectory(thd-modules)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)

