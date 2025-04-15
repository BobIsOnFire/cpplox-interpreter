find_package(FastFloat CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)

function(add_module_wrapper target external_target cppm)
  add_library(${target} OBJECT)
  target_sources(${target}
    PUBLIC
      FILE_SET CXX_MODULES FILES
        ${cppm}
  )
  target_link_libraries(${target} PRIVATE ${external_target})
endfunction()

add_module_wrapper(fast_float-mod FastFloat::fast_float fast_float.cppm)
add_module_wrapper(magic_enum-mod magic_enum::magic_enum magic_enum.cppm)
add_module_wrapper(yaml-cpp-mod yaml-cpp::yaml-cpp yaml-cpp.cppm)
