# Runs a program that must fail in one exact way: cmake -DPROGRAM=<file> -DARGUMENT=<arg> -DEXIT_CODE=<code>
# -DMESSAGE=<text> -P expect_failure.cmake. It passes only when the program exits with EXIT_CODE and its output contains
# MESSAGE, so a crash or another failure does not count.
execute_process(COMMAND "${PROGRAM}" "${ARGUMENT}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(NOT result STREQUAL "${EXIT_CODE}")
    message(FATAL_ERROR "${PROGRAM} exited with '${result}', not ${EXIT_CODE}:\n${output}")
endif()
string(FIND "${output}" "${MESSAGE}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "the output of ${PROGRAM} does not say '${MESSAGE}':\n${output}")
endif()
