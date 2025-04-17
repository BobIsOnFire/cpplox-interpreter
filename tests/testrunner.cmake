# Policy CMP0140: The return() command checks its arguments.
cmake_policy(SET CMP0140 NEW)

if(NOT DEFINED CPPLOX_EXE OR NOT DEFINED TEST_FILE)
    message(FATAL_ERROR "Usage: cmake -DCPPLOX_EXE=<exe> -DTEST_FILE=<file> -P testrunner.cmake")
endif()

cmake_path(GET TEST_FILE STEM LAST_ONLY file_stem)
cmake_path(GET TEST_FILE PARENT_PATH file_parent)
set(FILE_OUT "${TEST_FILE}.out")
set(FILE_ERR "${TEST_FILE}.err")

# Run the cpplox command and capture stdout and stderr
execute_process(
    COMMAND "${CPPLOX_EXE}" "${TEST_FILE}"
    OUTPUT_VARIABLE STDOUT
    ERROR_VARIABLE STDERR
)

function(get_file_contents filename outvar)
    if(EXISTS "${filename}")
        file(READ "${filename}" ${outvar})
    else()
        set(${outvar} "")
    endif()
    return(PROPAGATE ${outvar})
endfunction()

function(format_output str outvar)
    if (str STREQUAL "")
        set(str "<empty>")
    endif()
    # Prepend space to each line to make it formatted in the message() output
    string(REPLACE "\n" "\n " ${outvar} " ${str}")
    return(PROPAGATE ${outvar})
endfunction()

function(assert_equal expected actual assert_msg)
    if(NOT expected STREQUAL actual)
        format_output("${expected}" expected_out)
        format_output("${actual}" actual_out)
        message(FATAL_ERROR "${assert_msg}:\nExpected:\n${expected_out}\nActual:\n${actual_out}")
    endif()
endfunction()

# Compare stdout with <file>.out
get_file_contents("${FILE_OUT}" EXPECTED_STDOUT)
assert_equal("${EXPECTED_STDOUT}" "${STDOUT}" "Mismatch in stdout for ${TEST_FILE}")

# Compare stderr with <file>.err
get_file_contents("${FILE_ERR}" EXPECTED_STDERR)
assert_equal("${EXPECTED_STDERR}" "${STDERR}" "Mismatch in stderr for ${TEST_FILE}")

message(STATUS "Test passed for ${TEST_FILE}")
