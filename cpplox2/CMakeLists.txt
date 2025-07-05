add_library(cpplox2 STATIC)

target_sources(cpplox2
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      cpplox/Chunk.cpp
      cpplox/Debug.cpp
      cpplox/SourceLocation.cpp
      cpplox/Value.cpp
      cpplox.cppm
    BASE_DIRS . ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(cpplox2 PRIVATE
  magic_enum-mod
)

add_executable(cpplox2-exe)

target_sources(cpplox2-exe PRIVATE main.cpp)
target_link_libraries(cpplox2-exe PRIVATE cpplox2)
set_property(TARGET cpplox2-exe PROPERTY OUTPUT_NAME cpplox2)
