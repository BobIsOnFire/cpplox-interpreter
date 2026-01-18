include(FetchContent)

find_package(Boost CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)

# No Beman in upstream Conan, RIP
# https://discourse.bemanproject.org/t/make-beman-packageable
FetchContent_Declare(
    beman.inplace_vector
    GIT_REPOSITORY https://github.com/bemanproject/inplace_vector.git
    GIT_TAG ed4fc49909759ee043cdafbd2da8bdd499b52ba7
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(beman.inplace_vector)

function(add_module_wrapper target external_target cppm)
    add_library(${target} OBJECT)
    target_sources(${target} PUBLIC FILE_SET CXX_MODULES FILES ${cppm})
    target_link_libraries(${target} PRIVATE ${external_target})
endfunction()

add_module_wrapper(module.beman.inplace_vector beman::inplace_vector beman.inplace_vector.cppm)
add_module_wrapper(module.boost.container_hash Boost::headers boost.container_hash.cppm)
add_module_wrapper(module.magic_enum magic_enum::magic_enum magic_enum.cppm)
add_module_wrapper(module.yaml-cpp yaml-cpp::yaml-cpp yaml-cpp.cppm)
