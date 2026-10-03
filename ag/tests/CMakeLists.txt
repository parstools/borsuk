add_executable(agas_bootstrap_tests BootstrapFrontendTest.cpp)
target_link_libraries(agas_bootstrap_tests PRIVATE agas_bootstrap)
target_compile_definitions(agas_bootstrap_tests PRIVATE
    AGAS_TEST_GRAMMAR_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../grammars/Ag.ag"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_bootstrap_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME agas_bootstrap_tests COMMAND agas_bootstrap_tests)

add_executable(agas_lexer_oracle_tests LexerOracleTest.cpp)
target_link_libraries(agas_lexer_oracle_tests PRIVATE agas_bootstrap)
target_compile_definitions(agas_lexer_oracle_tests PRIVATE
    AGAS_TEST_GRAMMAR_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../grammars/Ag.ag"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_lexer_oracle_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_lexer_oracle_tests COMMAND agas_lexer_oracle_tests)
set_tests_properties(agas_lexer_oracle_tests PROPERTIES TIMEOUT 120)

add_executable(agas_production_metadata_tests ProductionMetadataTest.cpp)
target_link_libraries(agas_production_metadata_tests PRIVATE agas_bootstrap)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_production_metadata_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_production_metadata_tests
         COMMAND agas_production_metadata_tests)

add_executable(agas_artifact_generation_tests ArtifactGenerationTest.cpp)
target_link_libraries(agas_artifact_generation_tests PRIVATE agas_bootstrap)
target_compile_definitions(agas_artifact_generation_tests PRIVATE
    AGAS_TEST_GRAMMAR_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../grammars/Ag.ag"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_artifact_generation_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_artifact_generation_tests
         COMMAND agas_artifact_generation_tests)

add_executable(agas_reduction_program_tests ReductionProgramTest.cpp)
target_link_libraries(agas_reduction_program_tests PRIVATE agas_bootstrap)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_reduction_program_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_reduction_program_tests
         COMMAND agas_reduction_program_tests)

add_executable(agas_reduction_runtime_tests ReductionRuntimeTest.cpp)
target_link_libraries(agas_reduction_runtime_tests PRIVATE agas_bootstrap)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_reduction_runtime_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_reduction_runtime_tests
         COMMAND agas_reduction_runtime_tests)

add_executable(agas_ast_schema_tests AstSchemaTest.cpp)
target_link_libraries(agas_ast_schema_tests PRIVATE agas_bootstrap)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_ast_schema_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_ast_schema_tests COMMAND agas_ast_schema_tests)

add_executable(agas_ast_parser_tests AstParserTest.cpp)
target_link_libraries(agas_ast_parser_tests PRIVATE agas_bootstrap)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_ast_parser_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_ast_parser_tests COMMAND agas_ast_parser_tests)

add_executable(agas_syntax_adapter_tests AgSyntaxAdapterTest.cpp)
target_link_libraries(agas_syntax_adapter_tests PRIVATE agas_bootstrap)
target_compile_definitions(agas_syntax_adapter_tests PRIVATE
    AGAS_TEST_GRAMMAR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../grammars"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_syntax_adapter_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_syntax_adapter_tests COMMAND agas_syntax_adapter_tests)
set_tests_properties(agas_syntax_adapter_tests PROPERTIES TIMEOUT 120)

add_executable(agas_self_hosted_frontend_tests SelfHostedAgFrontendTest.cpp)
target_link_libraries(agas_self_hosted_frontend_tests PRIVATE agas_bootstrap)
target_compile_definitions(agas_self_hosted_frontend_tests PRIVATE
    AGAS_TEST_GRAMMAR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../grammars"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_self_hosted_frontend_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_self_hosted_frontend_tests
         COMMAND agas_self_hosted_frontend_tests)
set_tests_properties(agas_self_hosted_frontend_tests PROPERTIES TIMEOUT 120)

add_executable(agas_selective_merge_tests SelectiveMergeTest.cpp)
target_link_libraries(agas_selective_merge_tests PRIVATE agas_bootstrap)
target_include_directories(agas_selective_merge_tests PRIVATE
    "${AGAS_GENERATED_DIR}" "${AGAS_ZBIK_SOURCE_DIR}/tests")
target_include_directories(agas_selective_merge_tests SYSTEM PRIVATE "${ANTLR4_INCLUDE_DIR}")
target_compile_definitions(agas_selective_merge_tests PRIVATE
    AGAS_TEST_GRAMMAR_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../grammars/Ag.ag")
add_test(NAME agas_selective_merge_tests COMMAND agas_selective_merge_tests)
set_tests_properties(agas_selective_merge_tests PROPERTIES TIMEOUT 120)

set(AGAS_ACCEPTED_GRAMMARS
    Ag.ag
    CompressedTable.ag
    examples/cmm.ag
    examples/cminus.ag
    examples/cminus_closed_open.ag
    c/C.ag
    c/C90.ag
    c/C90SmallLegacy.ag
    c/C90LargeBranchLegacy.ag
    c/C90LargestExprLegacy.ag
    c/CDeclaratorSubset.ag
)

foreach(AGAS_GRAMMAR_PATH IN LISTS AGAS_ACCEPTED_GRAMMARS)
    string(MAKE_C_IDENTIFIER "${AGAS_GRAMMAR_PATH}" AGAS_GRAMMAR_TEST_NAME)
    add_test(
        NAME "agas_accepts_${AGAS_GRAMMAR_TEST_NAME}"
        COMMAND agas-bootstrap "${CMAKE_CURRENT_SOURCE_DIR}/../grammars/${AGAS_GRAMMAR_PATH}"
    )
    set_tests_properties("agas_accepts_${AGAS_GRAMMAR_TEST_NAME}" PROPERTIES
        PASS_REGULAR_EXPRESSION "grammar=[A-Za-z][A-Za-z_0-9]*"
    )
endforeach()

add_executable(agas_contextual_generation_tests ContextualGenerationTest.cpp)
target_link_libraries(agas_contextual_generation_tests PRIVATE agas_bootstrap)
target_compile_definitions(agas_contextual_generation_tests PRIVATE
    AGAS_CONTEXT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../grammars/contextual-lexer"
    AGAS_CONTEXT_PACKAGE="${CMAKE_CURRENT_SOURCE_DIR}/../artifacts/ag/v1")
add_test(NAME agas_contextual_generation_tests COMMAND agas_contextual_generation_tests)
