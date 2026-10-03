execute_process(
    COMMAND "${SEMA}" --legacy --emit-rust-dir "${OUTPUT_DIR}" "${SOURCE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
if(result EQUAL 0)
    message(FATAL_ERROR "Expected sema to reject ${SOURCE}")
endif()
string(FIND "${stderr}" "${EXPECTED_MESSAGE}" message_position)
if(message_position EQUAL -1)
    message(FATAL_ERROR "Wrong rejection for ${SOURCE}: ${stderr}")
endif()
