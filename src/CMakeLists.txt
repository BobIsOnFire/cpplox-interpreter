find_package(jinja2cpp CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)

add_executable(grammar_generator)
target_sources(grammar_generator
  PRIVATE
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      grammar/jinja2_yaml_binding.cpp
  PRIVATE 
    grammar/generate.cpp
)
target_link_libraries(grammar_generator PRIVATE jinja2cpp yaml-cpp::yaml-cpp)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Expr.cpp
  COMMAND grammar_generator
    grammar/grammar.yaml
    grammar/Expr.cpp.j2
    ${CMAKE_CURRENT_BINARY_DIR}/Expr.cpp
  DEPENDS
    grammar_generator
    grammar/grammar.yaml
    grammar/Expr.cpp.j2
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(cpplox STATIC)

target_sources(cpplox
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES 
      ${CMAKE_CURRENT_BINARY_DIR}/Expr.cpp
      cpplox/Lox.cpp
      cpplox/Scanner.cpp
      cpplox/Token.cpp
      cpplox/TokenType.cpp
      cpplox/exits.cpp
      cpplox.cppm
    BASE_DIRS . ${CMAKE_CURRENT_BINARY_DIR}
  PRIVATE
    cpplox/LoxImpl.cpp  
)

find_package(FastFloat CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)

target_link_libraries(cpplox PRIVATE
  FastFloat::fast_float
  magic_enum::magic_enum
)

add_executable(cpplox-exe)

target_sources(cpplox-exe PRIVATE main.cpp)
target_link_libraries(cpplox-exe PRIVATE cpplox)
set_property(TARGET cpplox-exe PROPERTY OUTPUT_NAME cpplox)
