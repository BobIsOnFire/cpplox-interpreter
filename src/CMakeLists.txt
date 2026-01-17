add_library(cpplox STATIC)

target_sources(cpplox
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      cpplox/Chunk.cppm
      cpplox/Code.cppm
      cpplox/Compiler.cppm
      cpplox/Debug.cppm
      cpplox/EnumFormatter.cppm
      cpplox/Obj.cppm
      cpplox/Object.cppm
      cpplox/OpCode.cppm
      cpplox/Scanner.cppm
      cpplox/SourceLocation.cppm
      cpplox/Token.cppm
      cpplox/Value.cppm
      cpplox/VirtualMachine.cppm
      cpplox/exits.cppm
      cpplox.cppm
    BASE_DIRS . ${CMAKE_CURRENT_BINARY_DIR}
  PRIVATE
    cpplox/Code.cpp
    cpplox/Compiler.cpp
    cpplox/Debug.cpp
    cpplox/Scanner.cpp
    cpplox/Value.cpp
    cpplox/VirtualMachine.cpp
)

target_link_libraries(cpplox PRIVATE
  Boost::headers # for Boost.Preprocessor
  module.boost.container_hash
  module.magic_enum
)

add_executable(cpplox-exe)

target_sources(cpplox-exe PRIVATE main.cpp)
target_link_libraries(cpplox-exe PRIVATE cpplox)
set_property(TARGET cpplox-exe PROPERTY OUTPUT_NAME cpplox)
