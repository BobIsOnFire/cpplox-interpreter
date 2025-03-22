add_executable(grammar_generator)
target_sources(grammar_generator
  PRIVATE
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      grammar/jinja2_yaml_binding.cpp
  PRIVATE 
    grammar/generate.cpp
)
target_link_libraries(grammar_generator PRIVATE jinja2cpp-mod yaml-cpp-mod)

foreach(source_name IN ITEMS Expr.cpp ExprSerializer.cpp)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${source_name}
    COMMAND grammar_generator
      grammar/expressions.yaml
      grammar/${source_name}.j2
      ${CMAKE_CURRENT_BINARY_DIR}/${source_name}
    DEPENDS
      grammar_generator
      grammar/expressions.yaml
      grammar/${source_name}.j2
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
endforeach()

foreach(source_name IN ITEMS Stmt.cpp StmtSerializer.cpp)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${source_name}
    COMMAND grammar_generator
      grammar/statements.yaml
      grammar/${source_name}.j2
      ${CMAKE_CURRENT_BINARY_DIR}/${source_name}
    DEPENDS
      grammar_generator
      grammar/statements.yaml
      grammar/${source_name}.j2
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
endforeach()

add_library(cpplox STATIC)

target_sources(cpplox
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES 
      ${CMAKE_CURRENT_BINARY_DIR}/Expr.cpp
      ${CMAKE_CURRENT_BINARY_DIR}/ExprSerializer.cpp
      ${CMAKE_CURRENT_BINARY_DIR}/Stmt.cpp
      ${CMAKE_CURRENT_BINARY_DIR}/StmtSerializer.cpp
      cpplox/Diagnostics.cpp
      cpplox/Environment.cpp
      cpplox/ExprOperandConverter.cpp
      cpplox/Interpreter.cpp
      cpplox/Lox.cpp
      cpplox/Parser.cpp
      cpplox/ParserError.cpp
      cpplox/RuntimeError.cpp
      cpplox/Scanner.cpp
      cpplox/ScopeExit.cpp
      cpplox/Token.cpp
      cpplox/TokenType.cpp
      cpplox/Value.cpp
      cpplox/exits.cpp
      cpplox.cppm
    BASE_DIRS . ${CMAKE_CURRENT_BINARY_DIR}
  PRIVATE
    cpplox/LoxImpl.cpp  
)

target_link_libraries(cpplox PRIVATE
  fast_float-mod
  magic_enum-mod
  yaml-cpp-mod
)

add_executable(cpplox-exe)

target_sources(cpplox-exe PRIVATE main.cpp)
target_link_libraries(cpplox-exe PRIVATE cpplox)
set_property(TARGET cpplox-exe PROPERTY OUTPUT_NAME cpplox)
