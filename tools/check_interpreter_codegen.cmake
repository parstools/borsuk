execute_process(
    COMMAND "${SEMA}" --legacy --emit-rust-dir "${OUTPUT_DIR}" "${SOURCE}"
    RESULT_VARIABLE generation_result
    ERROR_VARIABLE generation_stderr)
if(NOT generation_result EQUAL 0)
    message(FATAL_ERROR "Interpreter generation failed: ${generation_stderr}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files
        "${OUTPUT_DIR}/interpreter_gen.rs" "${EXPECTED}"
    RESULT_VARIABLE comparison_result)
if(NOT comparison_result EQUAL 0)
    message(FATAL_ERROR "Generated interpreter differs from ${EXPECTED}")
endif()
