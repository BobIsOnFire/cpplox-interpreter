# Installed by Conan
find_package(magic_enum CONFIG REQUIRED)

# Bring in module support (unfortunately not packaged yet by Conan)
file(DOWNLOAD
  https://raw.githubusercontent.com/Neargye/magic_enum/refs/tags/v${magic_enum_VERSION}/module/magic_enum.cppm
  ${CMAKE_BINARY_DIR}/magic_enum.cppm
)

add_library(magic_enum STATIC)
target_sources(magic_enum
  PUBLIC
    FILE_SET CXX_MODULES FILES
      ${CMAKE_BINARY_DIR}/magic_enum.cppm
)

target_compile_definitions(magic_enum PRIVATE MAGIC_ENUM_USE_STD_MODULE)
target_link_libraries(magic_enum PRIVATE magic_enum::magic_enum)
