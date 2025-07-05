file(GLOB_RECURSE TEST_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/testcases/*.lox")

foreach(file IN LISTS TEST_FILES)
    cmake_path(
        RELATIVE_PATH file
        BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/testcases"
        OUTPUT_VARIABLE test_name
    )
    add_test(
        NAME "${test_name}"
        COMMAND
            ${CMAKE_COMMAND} 
            -DCPPLOX_EXE=$<TARGET_FILE:cpplox-exe>
            -DTEST_FILE=${file}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/testrunner.cmake"
    )
    set_property(TEST "${test_name}"
        PROPERTY ENVIRONMENT
            UBSAN_OPTIONS=print_stacktrace=1
            # FIXME: a plethora of shared_ptrs in my Environment/Value design is causing a ton of
            # reference cycles and memory leaks. Perhaps second book chapter will show how to design
            # the interpreter without shared_ptrs :)
            ASAN_OPTIONS=detect_leaks=0
    )
endforeach()
