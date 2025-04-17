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
endforeach()
