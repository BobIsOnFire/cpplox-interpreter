add_executable(cpplox)

target_sources(cpplox
  PRIVATE
    FILE_SET cxx_modules TYPE CXX_MODULES FILES 
      Lox.cpp
      Scanner.cpp
      Token.cpp
      exits.cpp
  PRIVATE
    main.cpp
)
