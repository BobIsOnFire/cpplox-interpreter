find_package(Python3 REQUIRED)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Expr.cppm
    COMMAND
        Python3::Interpreter grammar/generate.py grammar/expressions.yaml
        grammar/Expr.cppm.j2 ${CMAKE_CURRENT_BINARY_DIR}/Expr.cppm
    DEPENDS grammar/generate.py grammar/expressions.yaml grammar/Expr.cppm.j2
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Stmt.cppm
    COMMAND
        Python3::Interpreter grammar/generate.py grammar/statements.yaml
        grammar/Stmt.cppm.j2 ${CMAKE_CURRENT_BINARY_DIR}/Stmt.cppm
    DEPENDS grammar/generate.py grammar/statements.yaml grammar/Stmt.cppm.j2
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(cpplox STATIC)

target_sources(
    cpplox
    PUBLIC
        FILE_SET cxx_modules
        TYPE CXX_MODULES
        FILES
            ${CMAKE_CURRENT_BINARY_DIR}/Expr.cppm
            ${CMAKE_CURRENT_BINARY_DIR}/Stmt.cppm
            cpplox/Chunk.cppm
            cpplox/Code.cppm
            cpplox/Compiler.cppm
            cpplox/Debug.cppm
            cpplox/EnumFormatter.cppm
            cpplox/Obj.cppm
            cpplox/Object.cppm
            cpplox/ObjReferenceTracer.cppm
            cpplox/OpCode.cppm
            cpplox/Parser.cppm
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
        cpplox/ObjReferenceTracer.cpp
        cpplox/Parser.cpp
        cpplox/Scanner.cpp
        cpplox/Value.cpp
        cpplox/VirtualMachine.cpp
)

target_link_libraries(
    cpplox
    PRIVATE
        Boost::headers # for Boost.Preprocessor
        module.beman.inplace_vector
        module.boost.container_hash
        module.magic_enum
)

add_executable(cpplox-exe)

target_sources(cpplox-exe PRIVATE main.cpp)
target_link_libraries(cpplox-exe PRIVATE cpplox)
set_property(TARGET cpplox-exe PROPERTY OUTPUT_NAME cpplox)
