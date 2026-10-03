if(DEFINED CONTRACTS)
    find_program(RUSTFMT rustfmt REQUIRED)
    set(generation_options --contracts "${CONTRACTS}")
    set(GRAMMAR "${OUTPUT_DIR}/parser-input.ag")
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")
    execute_process(
        COMMAND "${SEMA}" --force --emit-ag "${GRAMMAR}" "${SOURCE}"
        RESULT_VARIABLE projection_result
        ERROR_VARIABLE projection_stderr)
    if(NOT projection_result EQUAL 0)
        message(FATAL_ERROR "Ag projection failed: ${projection_stderr}")
    endif()
else()
    set(generation_options --legacy)
endif()
execute_process(
    COMMAND "${SEMA}" ${generation_options} --emit-rust-dir "${OUTPUT_DIR}" "${SOURCE}"
    RESULT_VARIABLE generation_result
    OUTPUT_VARIABLE generation_stdout
    ERROR_VARIABLE generation_stderr)
if(NOT generation_result EQUAL 0)
    message(FATAL_ERROR "Rust generation failed: ${generation_stderr}")
endif()

foreach(module_name IN ITEMS sema_gen.rs sema_lib_gen.rs interpreter_gen.rs interpreter_properties_gen.rs)
    if(module_name MATCHES "^interpreter_" AND
       NOT EXISTS "${EXPECTED_DIR}/${module_name}")
        continue()
    endif()
    set(expected_file "${EXPECTED_DIR}/${module_name}")
    if(DEFINED CONTRACTS)
        set(expected_file "${OUTPUT_DIR}/expected-${module_name}")
        configure_file("${EXPECTED_DIR}/${module_name}" "${expected_file}" COPYONLY)
        execute_process(
            COMMAND "${RUSTFMT}" --edition 2021 --config skip_children=true
                "${OUTPUT_DIR}/${module_name}" "${expected_file}"
            RESULT_VARIABLE format_result
            ERROR_VARIABLE format_stderr)
        if(NOT format_result EQUAL 0)
            message(FATAL_ERROR "Rust formatting failed: ${format_stderr}")
        endif()
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E compare_files
            "${OUTPUT_DIR}/${module_name}" "${expected_file}"
        RESULT_VARIABLE comparison_result)
    if(NOT comparison_result EQUAL 0)
        message(FATAL_ERROR "Generated Rust differs from ${EXPECTED_DIR}/${module_name}")
    endif()
endforeach()

execute_process(
    COMMAND "${AGAS}" --emit-rust-parser "${PARSER_OUTPUT}" "${GRAMMAR}"
    RESULT_VARIABLE parser_generation_result
    ERROR_VARIABLE parser_generation_stderr)
if(NOT parser_generation_result EQUAL 0)
    message(FATAL_ERROR "Agas parser generation failed: ${parser_generation_stderr}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${PARSER_OUTPUT}" "${PARSER_EXPECTED}"
    RESULT_VARIABLE parser_comparison_result)
if(NOT parser_comparison_result EQUAL 0)
    message(FATAL_ERROR "Generated parser differs from ${PARSER_EXPECTED}")
endif()
