set(expected ast-schema.json diagnostics.json lexer.json manifest.json parser.dsl
             productions.json reductions.json symbols.json)
file(GLOB reproduced RELATIVE "${REPRODUCED_DIR}" "${REPRODUCED_DIR}/*")
list(SORT reproduced)
if(NOT reproduced STREQUAL expected)
    message(FATAL_ERROR "Reproduced artifact has unexpected files: ${reproduced}")
endif()

foreach(name IN LISTS expected)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E compare_files
                "${REPRODUCED_DIR}/${name}" "${PINNED_DIR}/${name}"
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Reproduced artifact differs: ${name}")
    endif()
endforeach()

message(STATUS "All eight artifact files match the pinned package byte for byte")
