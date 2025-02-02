add_library(cpplox STATIC)

target_sources(cpplox
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES 
      cpplox/Lox.cpp
      cpplox/Scanner.cpp
      cpplox/Token.cpp
      cpplox/exits.cpp
      cpplox.cppm
)

add_executable(cpplox-exe)

target_sources(cpplox-exe PRIVATE main.cpp)
target_link_libraries(cpplox-exe PRIVATE cpplox)
set_property(TARGET cpplox-exe PROPERTY OUTPUT_NAME cpplox)
