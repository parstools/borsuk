cmake_minimum_required(VERSION 4.0)

project(borsuk VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(AGAS_BUILD_ANTLR_BOOTSTRAP
       "Build the reproducible ANTLR C++ bootstrap frontend" OFF)

set(AGAS_ZBIK_REVISION "" CACHE STRING
    "Stable Zbik revision recorded in generated parser artifacts")
if(NOT AGAS_ZBIK_REVISION)
    execute_process(
        COMMAND git -C "${AGAS_ZBIK_SOURCE_DIR}" rev-parse HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE AGAS_ZBIK_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE AGAS_ZBIK_REVISION_RESULT
    )
    if(NOT AGAS_ZBIK_REVISION_RESULT EQUAL 0 OR NOT AGAS_ZBIK_REVISION)
        set(AGAS_ZBIK_REVISION "unversioned-zbik-source")
    endif()
endif()

set(AGAS_ZBIK_SOURCE_DIR "${PROJECT_SOURCE_DIR}/zbik" CACHE PATH
    "Path to the Zbik Git submodule")
if(NOT EXISTS "${AGAS_ZBIK_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Zbik source tree not found: ${AGAS_ZBIK_SOURCE_DIR}")
endif()

set(AGAS_SAVED_BUILD_TESTING "${BUILD_TESTING}")
set(BUILD_TESTING OFF)
add_subdirectory("${AGAS_ZBIK_SOURCE_DIR}"
                 "${CMAKE_CURRENT_BINARY_DIR}/zbik" EXCLUDE_FROM_ALL)
set(BUILD_TESTING "${AGAS_SAVED_BUILD_TESTING}")
unset(AGAS_SAVED_BUILD_TESTING)

find_package(nlohmann_json 3.11 REQUIRED)

add_library(agas_core
    ag/src/artifact/AstSchemaSection.cpp
    ag/src/artifact/ArtifactPackage.cpp
    ag/src/artifact/ArtifactManifest.cpp
    ag/src/artifact/ArtifactManifestJson.cpp
    ag/src/artifact/GrammarSections.cpp
    ag/src/artifact/LexerSection.cpp
    ag/src/artifact/LexerSectionJson.cpp
    ag/src/artifact/ParserTableSection.cpp
    ag/src/artifact/ReductionSection.cpp
    ag/src/artifact/Sha256.cpp
    ag/src/generator/ArtifactGeneration.cpp
    ag/src/generator/AstSchema.cpp
    ag/src/generator/LexerGeneration.cpp
    ag/src/generator/ContextualGeneration.cpp
    ag/src/generator/ParserGeneration.cpp
    ag/src/generator/ProductionMetadata.cpp
    ag/src/generator/ReductionProgram.cpp
    ag/src/generator/StaticRustParserExport.cpp
    ag/src/generator/TableExport.cpp
    ag/src/model/BnfLowering.cpp
    ag/src/model/AstChain.cpp
    ag/src/model/Validation.cpp
    ag/src/runtime/ArtifactAstParser.cpp
    ag/src/runtime/AstWireJson.cpp
    ag/src/runtime/ReductionRuntime.cpp
    ag/src/runtime/ArtifactLexerRuntime.cpp
    ag/src/runtime/AstParser.cpp
    ag/src/runtime/AgSyntaxAdapter.cpp
    ag/src/runtime/PackagedAgFrontend.cpp
    ag/src/runtime/SourceText.cpp
    ag/src/runtime/SelfHostedAgFrontend.cpp
)

target_include_directories(agas_core
    PUBLIC "${PROJECT_SOURCE_DIR}/ag/include"
)
target_link_libraries(agas_core PUBLIC zbik_core nlohmann_json::nlohmann_json)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(agas_cli_support STATIC
    ag/src/cli/Help.cpp
)
target_include_directories(agas_cli_support
    PUBLIC "${PROJECT_SOURCE_DIR}/ag/src"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_cli_support PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
add_executable(agas_ast_promotion_tests ag/tests/AstPromotionTest.cpp)
target_link_libraries(agas_ast_promotion_tests PRIVATE agas_core)
target_compile_definitions(agas_ast_promotion_tests PRIVATE
    AGAS_PINNED_ARTIFACT_DIR="${PROJECT_SOURCE_DIR}/ag/ag/artifacts/ag/v1")
add_test(NAME agas_ast_promotion_tests COMMAND agas_ast_promotion_tests)
add_executable(agas_artifact_manifest_tests ag/tests/ArtifactManifestTest.cpp)
target_link_libraries(agas_artifact_manifest_tests PRIVATE agas_core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_artifact_manifest_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_artifact_manifest_tests
         COMMAND agas_artifact_manifest_tests)

add_executable(agas_artifact_package_tests ag/tests/ArtifactPackageTest.cpp)
target_link_libraries(agas_artifact_package_tests PRIVATE agas_core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_artifact_package_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_artifact_package_tests COMMAND agas_artifact_package_tests)

add_executable(agas_artifact_reproduction_tests
    ag/tests/ArtifactReproductionTest.cpp)
target_link_libraries(agas_artifact_reproduction_tests PRIVATE agas_core)
target_compile_definitions(agas_artifact_reproduction_tests PRIVATE
    AGAS_TEST_GRAMMAR_FILE="${PROJECT_SOURCE_DIR}/ag/ag/grammars/Ag.ag"
    AGAS_PINNED_ARTIFACT_DIR="${PROJECT_SOURCE_DIR}/ag/ag/artifacts/ag/v1"
    AGAS_GENERATOR_VERSION="${PROJECT_VERSION}"
    AGAS_ZBIK_REVISION="${AGAS_ZBIK_REVISION}"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_artifact_reproduction_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_artifact_reproduction_tests
         COMMAND agas_artifact_reproduction_tests)

add_executable(agas_grammar_sections_tests ag/tests/GrammarSectionsTest.cpp)
target_link_libraries(agas_grammar_sections_tests PRIVATE agas_core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_grammar_sections_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_grammar_sections_tests COMMAND agas_grammar_sections_tests)

add_executable(agas_parser_table_section_tests
    ag/tests/ParserTableSectionTest.cpp)
target_link_libraries(agas_parser_table_section_tests PRIVATE agas_core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_parser_table_section_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_parser_table_section_tests
         COMMAND agas_parser_table_section_tests)

add_executable(agas_artifact_ast_parser_tests
    ag/tests/ArtifactAstParserTest.cpp)
target_link_libraries(agas_artifact_ast_parser_tests PRIVATE agas_core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_artifact_ast_parser_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_artifact_ast_parser_tests
         COMMAND agas_artifact_ast_parser_tests)

add_executable(agas_artifact_lexer_runtime_tests
    ag/tests/ArtifactLexerRuntimeTest.cpp)
target_link_libraries(agas_artifact_lexer_runtime_tests PRIVATE agas_core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_artifact_lexer_runtime_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_artifact_lexer_runtime_tests
         COMMAND agas_artifact_lexer_runtime_tests)

add_executable(agas_packaged_frontend_tests
    ag/tests/PackagedAgFrontendTest.cpp)
target_link_libraries(agas_packaged_frontend_tests PRIVATE agas_core)
target_compile_definitions(agas_packaged_frontend_tests PRIVATE
    AGAS_TEST_GRAMMAR_FILE="${PROJECT_SOURCE_DIR}/ag/ag/grammars/Ag.ag"
    AGAS_PINNED_ARTIFACT_DIR="${PROJECT_SOURCE_DIR}/ag/ag/artifacts/ag/v1"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_packaged_frontend_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_packaged_frontend_tests
         COMMAND agas_packaged_frontend_tests)

add_executable(agas_artifact_runtime_parity_tests
    ag/tests/ArtifactRuntimeParityTest.cpp)
target_link_libraries(agas_artifact_runtime_parity_tests PRIVATE agas_core)
target_compile_definitions(agas_artifact_runtime_parity_tests PRIVATE
    AGAS_TEST_GRAMMAR_FILE="${PROJECT_SOURCE_DIR}/ag/ag/grammars/Ag.ag"
    AGAS_PINNED_ARTIFACT_DIR="${PROJECT_SOURCE_DIR}/ag/ag/artifacts/ag/v1"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_artifact_runtime_parity_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_artifact_runtime_parity_tests
         COMMAND agas_artifact_runtime_parity_tests)

add_executable(agas_static_rust_parser_export_tests
    ag/tests/StaticRustParserExportTest.cpp)
target_link_libraries(agas_static_rust_parser_export_tests PRIVATE agas_core)
target_compile_definitions(agas_static_rust_parser_export_tests PRIVATE
    AGAS_PINNED_ARTIFACT_DIR="${PROJECT_SOURCE_DIR}/ag/ag/artifacts/ag/v1"
    AGAS_COMPRESSED_TABLE_GRAMMAR="${PROJECT_SOURCE_DIR}/ag/ag/grammars/CompressedTable.ag"
    AGAS_GENERATED_RUST_PARSER="${PROJECT_SOURCE_DIR}/rust/crates/ag-runtime/src/generated/compressed_table.rs"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_static_rust_parser_export_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_static_rust_parser_export_tests
         COMMAND agas_static_rust_parser_export_tests)

add_executable(agas_conflict_policy_tests ag/tests/ConflictPolicyTest.cpp)
target_link_libraries(agas_conflict_policy_tests PRIVATE agas_core)
target_compile_definitions(agas_conflict_policy_tests PRIVATE
    AGAS_PINNED_ARTIFACT_DIR="${PROJECT_SOURCE_DIR}/ag/ag/artifacts/ag/v1"
    AGAS_IF_ELSE_GRAMMAR="${PROJECT_SOURCE_DIR}/ag/ag/grammars/examples/if-else.ag"
    AGAS_RESOLVED_IF_ELSE_GRAMMAR="${PROJECT_SOURCE_DIR}/ag/ag/grammars/examples/if-else-prefer-shift.ag"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_conflict_policy_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_conflict_policy_tests COMMAND agas_conflict_policy_tests)

add_custom_target(agas_check_reproducibility
    COMMAND $<TARGET_FILE:agas_artifact_reproduction_tests> --full
    COMMAND $<TARGET_FILE:agas_static_rust_parser_export_tests> --full
    DEPENDS agas_artifact_reproduction_tests
            agas_static_rust_parser_export_tests
    COMMENT "Reproducing pinned Agas artifacts with the self-hosted frontend"
    VERBATIM
)

add_executable(agas ag/src/agas_main.cpp)
target_link_libraries(agas PRIVATE agas_core agas_cli_support)
target_compile_definitions(agas PRIVATE
    AGAS_DEFAULT_GRAMMAR_FILE="${PROJECT_SOURCE_DIR}/ag/ag/grammars/Ag.ag"
    AGAS_DEFAULT_ARTIFACT_DIR="${PROJECT_SOURCE_DIR}/ag/ag/artifacts/ag/v1"
    AGAS_GENERATOR_VERSION="${PROJECT_VERSION}"
    AGAS_ZBIK_REVISION="${AGAS_ZBIK_REVISION}"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_executable(ag ag/src/agas_main.cpp)
target_link_libraries(ag PRIVATE agas_core agas_cli_support)
target_compile_definitions(ag PRIVATE
    AGAS_DEFAULT_GRAMMAR_FILE="${PROJECT_SOURCE_DIR}/ag/ag/grammars/Ag.ag"
    AGAS_DEFAULT_ARTIFACT_DIR="${PROJECT_SOURCE_DIR}/ag/ag/artifacts/ag/v1"
    AGAS_GENERATOR_VERSION="${PROJECT_VERSION}"
    AGAS_ZBIK_REVISION="${AGAS_ZBIK_REVISION}"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME agas_uses_pinned_artifact
         COMMAND agas "${PROJECT_SOURCE_DIR}/ag/ag/grammars/Ag.ag")
set_tests_properties(agas_uses_pinned_artifact PROPERTIES
    PASS_REGULAR_EXPRESSION "grammar=Ag")

set(SEMA_GRAMMAR_FILE "${PROJECT_SOURCE_DIR}/sema/grammars/Sema.ag")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${SEMA_GRAMMAR_FILE}")
file(SHA256 "${SEMA_GRAMMAR_FILE}" SEMA_SOURCE_SHA256)
set(SEMA_ARTIFACT_DIR "${CMAKE_CURRENT_BINARY_DIR}/sema-package")
add_custom_command(
    OUTPUT "${SEMA_ARTIFACT_DIR}/manifest.json"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${SEMA_ARTIFACT_DIR}"
    COMMAND $<TARGET_FILE:agas> --emit-package
            "${SEMA_ARTIFACT_DIR}" "${SEMA_GRAMMAR_FILE}"
    DEPENDS agas "${SEMA_GRAMMAR_FILE}"
    COMMENT "Generating Sema parser artifact"
    VERBATIM
)
add_custom_target(sema_artifact
    DEPENDS "${SEMA_ARTIFACT_DIR}/manifest.json")
add_library(agsem_core STATIC
    "${PROJECT_SOURCE_DIR}/sema/src/Completeness.cpp"
    "${PROJECT_SOURCE_DIR}/sema/src/CompletenessReport.cpp"
    "${PROJECT_SOURCE_DIR}/sema/src/SemanticCompleteness.cpp"
    "${PROJECT_SOURCE_DIR}/sema/src/Template.cpp"
    "${PROJECT_SOURCE_DIR}/sema/src/ModelBindings.cpp"
    "${PROJECT_SOURCE_DIR}/sema/src/ModelBindingSchema.cpp"
    "${PROJECT_SOURCE_DIR}/sema/src/Projection.cpp"
    "${PROJECT_SOURCE_DIR}/sema/src/ContractEnvironment.cpp"
    "${PROJECT_SOURCE_DIR}/sema/src/SemanticBinding.cpp"
    "${PROJECT_SOURCE_DIR}/sema/src/DocumentModel.cpp"
    "${PROJECT_SOURCE_DIR}/sema/src/DocumentGrammar.cpp"
    "${PROJECT_SOURCE_DIR}/sema/src/DocumentFrontend.cpp"
    "${PROJECT_SOURCE_DIR}/sema/src/SemanticInput.cpp"
    "${PROJECT_SOURCE_DIR}/sema/src/SemanticValidation.cpp"
    "${PROJECT_SOURCE_DIR}/sema/src/RustGenerator.cpp"
    "${PROJECT_SOURCE_DIR}/sema/src/SemanticPolicies.cpp"
    "${PROJECT_SOURCE_DIR}/sema/src/AssignmentPolicies.cpp"
    "${PROJECT_SOURCE_DIR}/sema/src/ConditionPolicies.cpp")
target_include_directories(agsem_core PUBLIC
    "${PROJECT_SOURCE_DIR}/sema/src"
    "${PROJECT_SOURCE_DIR}/sema/include")
target_link_libraries(agsem_core PUBLIC agas_core)
add_executable(agsem_compose_document
    "${PROJECT_SOURCE_DIR}/sema/src/compose_document.cpp"
    "${PROJECT_SOURCE_DIR}/sema/src/DocumentGrammar.cpp")
target_include_directories(agsem_compose_document PRIVATE "${PROJECT_SOURCE_DIR}/sema/include")
target_link_libraries(agsem_compose_document PRIVATE agas_core)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/ag/grammars/Ag.ag"
    "${PROJECT_SOURCE_DIR}/sema/grammars/Document.ag")
file(READ "${PROJECT_SOURCE_DIR}/ag/grammars/Ag.ag" SEMA_DOCUMENT_AG_SOURCE)
file(READ "${SEMA_GRAMMAR_FILE}" SEMA_DOCUMENT_ACTION_SOURCE)
file(READ "${PROJECT_SOURCE_DIR}/sema/grammars/Document.ag" SEMA_DOCUMENT_ENVELOPE_SOURCE)
string(SHA256 SEMA_DOCUMENT_SOURCE_SHA256 "${SEMA_DOCUMENT_AG_SOURCE}${SEMA_DOCUMENT_ACTION_SOURCE}${SEMA_DOCUMENT_ENVELOPE_SOURCE}")
set(SEMA_DOCUMENT_ARTIFACT_DIR "${CMAKE_CURRENT_BINARY_DIR}/sema-document-package")
add_custom_command(
    OUTPUT "${SEMA_DOCUMENT_ARTIFACT_DIR}/manifest.json"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${SEMA_DOCUMENT_ARTIFACT_DIR}"
    COMMAND $<TARGET_FILE:agsem_compose_document>
            "${PROJECT_SOURCE_DIR}/ag/artifacts/ag/v1"
            "${PROJECT_SOURCE_DIR}/ag/grammars/Ag.ag"
            "${SEMA_GRAMMAR_FILE}"
            "${PROJECT_SOURCE_DIR}/sema/grammars/Document.ag"
            "${SEMA_DOCUMENT_ARTIFACT_DIR}"
    DEPENDS agsem_compose_document "${SEMA_GRAMMAR_FILE}"
            "${PROJECT_SOURCE_DIR}/ag/grammars/Ag.ag"
            "${PROJECT_SOURCE_DIR}/sema/grammars/Document.ag"
    COMMENT "Composing the version 1 document parser from Ag and Sema extensions"
    VERBATIM)
add_custom_target(sema_document_artifact
    DEPENDS "${SEMA_DOCUMENT_ARTIFACT_DIR}/manifest.json")
add_library(coge_core STATIC
    "${PROJECT_SOURCE_DIR}/coge/src/CogeTemplate.cpp"
    "${PROJECT_SOURCE_DIR}/coge/src/CogeCompleteness.cpp"
    "${PROJECT_SOURCE_DIR}/coge/src/CogeBinding.cpp"
    "${PROJECT_SOURCE_DIR}/coge/src/CogeExecutionPattern.cpp"
    "${PROJECT_SOURCE_DIR}/coge/src/CogeExecutionModel.cpp"
    "${PROJECT_SOURCE_DIR}/coge/src/CogeExecutionProfiles.cpp"
    "${PROJECT_SOURCE_DIR}/coge/src/CogeProjection.cpp"
    "${PROJECT_SOURCE_DIR}/coge/src/CogeDocumentModel.cpp"
    "${PROJECT_SOURCE_DIR}/coge/src/CogeBackendRust.cpp"
    "${PROJECT_SOURCE_DIR}/coge/src/CogeLoweringRust.cpp"
    "${PROJECT_SOURCE_DIR}/coge/src/CogePropertyExpression.cpp"
    "${PROJECT_SOURCE_DIR}/coge/src/CogePropertiesRust.cpp"
    "${PROJECT_SOURCE_DIR}/coge/src/CogeExecutionType.cpp"
    "${PROJECT_SOURCE_DIR}/coge/src/CogeExecutionDeclarationsRust.cpp"
    "${PROJECT_SOURCE_DIR}/coge/src/CogeInterpreterRust.cpp"
    "${PROJECT_SOURCE_DIR}/coge/src/CogeExecutionExpressionRust.cpp")
target_sources(coge_core PRIVATE
    "${PROJECT_SOURCE_DIR}/coge/src/CogeExecutionStatementRust.cpp"
    "${PROJECT_SOURCE_DIR}/coge/src/CogeCheckedGeneration.cpp"
    "${PROJECT_SOURCE_DIR}/coge/src/CogePreparation.cpp")
target_sources(coge_core PRIVATE
    "${PROJECT_SOURCE_DIR}/coge/src/CogeLegacyAdapter.cpp")
target_include_directories(coge_core PUBLIC
    "${PROJECT_SOURCE_DIR}/sema/include"
    "${PROJECT_SOURCE_DIR}/coge/include")
target_link_libraries(coge_core PUBLIC agsem_core)
add_executable(coge_backend_validation_tests
    "${PROJECT_SOURCE_DIR}/tests/integration/CogeBackendTest.cpp")
target_link_libraries(coge_backend_validation_tests PRIVATE coge_core)
add_test(NAME coge_backend_validation_tests
    COMMAND coge_backend_validation_tests)
add_executable(coge_execution_expression_tests
    "${PROJECT_SOURCE_DIR}/tests/integration/CogeExecutionExpressionTest.cpp")
target_link_libraries(coge_execution_expression_tests PRIVATE coge_core)
add_test(NAME coge_execution_expression_tests
    COMMAND coge_execution_expression_tests)
add_executable(coge_interpreter_generation_tests
    "${PROJECT_SOURCE_DIR}/tests/integration/CogeInterpreterTest.cpp")
target_link_libraries(coge_interpreter_generation_tests PRIVATE coge_core)
add_test(NAME coge_interpreter_generation_tests
    COMMAND coge_interpreter_generation_tests)
add_executable(coge_execution_type_tests
    "${PROJECT_SOURCE_DIR}/tests/integration/CogeExecutionTypeTest.cpp")
target_link_libraries(coge_execution_type_tests PRIVATE coge_core)
add_test(NAME coge_execution_type_tests
    COMMAND coge_execution_type_tests)
add_executable(coge_property_expression_tests
    "${PROJECT_SOURCE_DIR}/tests/integration/CogePropertyExpressionTest.cpp")
target_link_libraries(coge_property_expression_tests PRIVATE coge_core)
add_test(NAME coge_property_expression_tests
    COMMAND coge_property_expression_tests)
add_executable(coge_lowering_validation_tests
    "${PROJECT_SOURCE_DIR}/tests/integration/CogeLoweringTest.cpp")
target_link_libraries(coge_lowering_validation_tests PRIVATE coge_core)
add_test(NAME coge_lowering_validation_tests
    COMMAND coge_lowering_validation_tests)
add_executable(agsem_semantic_input_tests
    "${PROJECT_SOURCE_DIR}/tests/integration/SemanticInputTest.cpp")
target_link_libraries(agsem_semantic_input_tests PRIVATE agsem_core)
add_test(NAME agsem_semantic_input_tests
    COMMAND agsem_semantic_input_tests)
add_executable(agsem_core_validation_tests
    "${PROJECT_SOURCE_DIR}/tests/integration/SemanticValidationTest.cpp")
target_link_libraries(agsem_core_validation_tests PRIVATE agsem_core)
add_dependencies(agsem_core_validation_tests sema_artifact)
add_test(NAME agsem_core_validation_tests
    COMMAND agsem_core_validation_tests
        "${SEMA_ARTIFACT_DIR}"
        "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/functions.sema"
        "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/function_cycle.sema"
        "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/function_unresolved.sema")
add_executable(agsem_checked_semantics_tests
    "${PROJECT_SOURCE_DIR}/tests/integration/CheckedSemanticsTest.cpp")
target_link_libraries(agsem_checked_semantics_tests PRIVATE agsem_core)
add_dependencies(agsem_checked_semantics_tests sema_artifact)
add_test(NAME agsem_checked_semantics_tests
    COMMAND agsem_checked_semantics_tests
        "${SEMA_ARTIFACT_DIR}"
        "${PROJECT_SOURCE_DIR}/ag/artifacts/ag/v1"
        "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/scope_codegen.sema"
        "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/scope_codegen.ag")
add_executable(agsem_prepared_properties_tests
    "${PROJECT_SOURCE_DIR}/tests/integration/PreparedPropertiesTest.cpp")
target_link_libraries(agsem_prepared_properties_tests PRIVATE
    coge_core)
add_dependencies(agsem_prepared_properties_tests sema_artifact)
add_test(NAME agsem_prepared_properties_tests
    COMMAND agsem_prepared_properties_tests
        "${SEMA_ARTIFACT_DIR}"
        "${PROJECT_SOURCE_DIR}/ag/artifacts/ag/v1"
        "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/toyc_full.sema"
        "${PROJECT_SOURCE_DIR}/ag/grammars/examples/toyc.ag")
add_executable(agsem_prepared_function_plan_tests
    "${PROJECT_SOURCE_DIR}/tests/integration/PreparedFunctionPlanTest.cpp")
target_link_libraries(agsem_prepared_function_plan_tests PRIVATE
    coge_core)
add_dependencies(agsem_prepared_function_plan_tests sema_artifact)
add_test(NAME agsem_prepared_function_plan_tests
    COMMAND agsem_prepared_function_plan_tests
        "${SEMA_ARTIFACT_DIR}"
        "${PROJECT_SOURCE_DIR}/ag/artifacts/ag/v1"
        "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/scope_codegen.sema"
        "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/scope_codegen.ag")
add_executable(agsem_semantic_binding_tests "${PROJECT_SOURCE_DIR}/tests/integration/SemanticBindingTest.cpp")
target_link_libraries(agsem_semantic_binding_tests PRIVATE coge_core)
add_dependencies(agsem_semantic_binding_tests sema_document_artifact)
add_test(NAME agsem_semantic_binding_tests COMMAND agsem_semantic_binding_tests
    "${SEMA_DOCUMENT_ARTIFACT_DIR}" "${PROJECT_SOURCE_DIR}/ag/artifacts/ag/v1")
add_executable(agsem_document_model_tests "${PROJECT_SOURCE_DIR}/tests/integration/DocumentModelTest.cpp")
target_link_libraries(agsem_document_model_tests PRIVATE coge_core)
add_dependencies(agsem_document_model_tests sema_document_artifact)
add_test(NAME agsem_document_model_tests COMMAND agsem_document_model_tests
    "${SEMA_DOCUMENT_ARTIFACT_DIR}" "${PROJECT_SOURCE_DIR}/ag/artifacts/ag/v1")
add_executable(agsem_document_frontend_tests "${PROJECT_SOURCE_DIR}/tests/integration/DocumentFrontendTest.cpp")
target_include_directories(agsem_document_frontend_tests PRIVATE "${PROJECT_SOURCE_DIR}/sema/src")
target_link_libraries(agsem_document_frontend_tests PRIVATE coge_core)
add_dependencies(agsem_document_frontend_tests sema_document_artifact sema_artifact)
add_test(NAME agsem_document_frontend_tests COMMAND agsem_document_frontend_tests
    "${SEMA_DOCUMENT_ARTIFACT_DIR}" "${PROJECT_SOURCE_DIR}/ag/artifacts/ag/v1"
    "${SEMA_ARTIFACT_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")
add_library(agsem_document_cli STATIC "${PROJECT_SOURCE_DIR}/sema/src/DocumentCli.cpp")
target_link_libraries(agsem_document_cli PUBLIC agsem_core)
target_compile_definitions(agsem_document_cli PRIVATE
    SEMA_DOCUMENT_ARTIFACT_DIR="${SEMA_DOCUMENT_ARTIFACT_DIR}"
    SEMA_DOCUMENT_SOURCE_SHA256="${SEMA_DOCUMENT_SOURCE_SHA256}"
    AGAS_PINNED_ARTIFACT_DIR="${PROJECT_SOURCE_DIR}/ag/artifacts/ag/v1")
add_executable(sema "${PROJECT_SOURCE_DIR}/sema/src/sema.cpp")
target_link_libraries(sema PRIVATE agsem_document_cli)
add_dependencies(sema sema_document_artifact)
add_executable(coge "${PROJECT_SOURCE_DIR}/coge/src/coge.cpp"
    "${PROJECT_SOURCE_DIR}/coge/src/CogeLegacyCli.cpp")
target_link_libraries(coge PRIVATE agsem_document_cli coge_core)
target_compile_definitions(coge PRIVATE
    SEMA_DEFAULT_ARTIFACT_DIR="${SEMA_ARTIFACT_DIR}"
    SEMA_SOURCE_SHA256="${SEMA_SOURCE_SHA256}"
    SEMA_DOCUMENT_ARTIFACT_DIR="${SEMA_DOCUMENT_ARTIFACT_DIR}"
    SEMA_DOCUMENT_SOURCE_SHA256="${SEMA_DOCUMENT_SOURCE_SHA256}"
    AGAS_PINNED_ARTIFACT_DIR="${PROJECT_SOURCE_DIR}/ag/artifacts/ag/v1")
add_dependencies(coge sema_artifact sema_document_artifact)
add_test(NAME sema_requires_explicit_legacy
    COMMAND sema "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/minimal.sema")
set_tests_properties(sema_requires_explicit_legacy PROPERTIES WILL_FAIL TRUE)
add_test(NAME sema_parses_standalone
    COMMAND sema "${PROJECT_SOURCE_DIR}/tests/integration/standalone.sema")
set_tests_properties(sema_parses_standalone PROPERTIES PASS_REGULAR_EXPRESSION "check OK")
add_test(NAME sema_return_policy
         COMMAND python3 "${PROJECT_SOURCE_DIR}/tools/check_return_policy.py"
                 --coge $<TARGET_FILE:coge>
                 --source "${PROJECT_SOURCE_DIR}/coge/examples/toyc/toyc.coge"
                 --contracts "${PROJECT_SOURCE_DIR}/contracts/toyc-runtime-v1.json")
add_test(NAME sema_assignment_policy
         COMMAND python3 "${PROJECT_SOURCE_DIR}/tools/check_assignment_policy.py"
                 --coge $<TARGET_FILE:coge>
                 --source "${PROJECT_SOURCE_DIR}/coge/examples/toyc/toyc.coge"
                 --contracts "${PROJECT_SOURCE_DIR}/contracts/toyc-runtime-v1.json")
add_test(NAME sema_condition_policy
         COMMAND python3 "${PROJECT_SOURCE_DIR}/tools/check_condition_policy.py"
                 --coge $<TARGET_FILE:coge>
                 --source "${PROJECT_SOURCE_DIR}/coge/examples/toyc/toyc.coge"
                 --contracts "${PROJECT_SOURCE_DIR}/contracts/toyc-runtime-v1.json")
add_test(NAME sema_statement_ir
         COMMAND python3 "${PROJECT_SOURCE_DIR}/tools/check_statement_ir.py"
                 --coge $<TARGET_FILE:coge>
                 --source "${PROJECT_SOURCE_DIR}/coge/examples/toyc/toyc.coge"
                 --contracts "${PROJECT_SOURCE_DIR}/contracts/toyc-runtime-v1.json")
add_test(NAME sema_flow_actions
         COMMAND python3 "${PROJECT_SOURCE_DIR}/tools/check_flow_actions.py"
                 --coge $<TARGET_FILE:coge>
                 --source "${PROJECT_SOURCE_DIR}/coge/examples/toyc/toyc.coge"
                 --contracts "${PROJECT_SOURCE_DIR}/contracts/toyc-runtime-v1.json")
add_test(NAME sema_selection_policy
         COMMAND python3 "${PROJECT_SOURCE_DIR}/tools/check_selection_policy.py"
                 --coge $<TARGET_FILE:coge>
                 --source "${PROJECT_SOURCE_DIR}/coge/examples/toycp/toycp.coge"
                 --contracts "${PROJECT_SOURCE_DIR}/contracts/toycp-runtime-v1.json")
add_test(NAME sema_lowering_model
         COMMAND python3 "${PROJECT_SOURCE_DIR}/tools/check_lowering_model.py"
                 --coge $<TARGET_FILE:coge>
                 --source "${PROJECT_SOURCE_DIR}/coge/examples/toyc/toyc.coge"
                 --contracts "${PROJECT_SOURCE_DIR}/contracts/toyc-runtime-v1.json")
add_test(NAME agsem_model_binding_tests
         COMMAND python3 "${PROJECT_SOURCE_DIR}/tests/integration/check_model_bindings.py"
                 --coge $<TARGET_FILE:coge> --sema $<TARGET_FILE:sema>
                 --root "${PROJECT_SOURCE_DIR}")
add_executable(coge_execution_profile_model_tests "${PROJECT_SOURCE_DIR}/tests/integration/ExecutionProfileTest.cpp")
target_link_libraries(coge_execution_profile_model_tests PRIVATE coge_core)
target_include_directories(coge_execution_profile_model_tests PRIVATE "${PROJECT_SOURCE_DIR}/sema/src")
add_dependencies(coge_execution_profile_model_tests sema_document_artifact)
add_test(NAME coge_execution_profile_model_tests COMMAND coge_execution_profile_model_tests
         "${CMAKE_CURRENT_BINARY_DIR}/sema-document-package" "${PROJECT_SOURCE_DIR}/ag/artifacts/ag/v1"
         "${PROJECT_SOURCE_DIR}/coge/examples/toyc/toyc.coge"
         "${PROJECT_SOURCE_DIR}/contracts/toyc-runtime-v1.json")
add_test(NAME coge_execution_profiles
         COMMAND python3 "${PROJECT_SOURCE_DIR}/tests/integration/check_execution_profiles.py"
                 --coge $<TARGET_FILE:coge> --sema $<TARGET_FILE:sema>
                 --root "${PROJECT_SOURCE_DIR}")
set_tests_properties(coge_execution_profiles PROPERTIES TIMEOUT 300)
add_test(NAME coge_lowering_profiles
         COMMAND python3 "${PROJECT_SOURCE_DIR}/tests/integration/check_lowering_profiles.py"
                 --coge $<TARGET_FILE:coge>
                 --source "${PROJECT_SOURCE_DIR}/coge/examples/toyc/toyc.coge"
                 --contracts "${PROJECT_SOURCE_DIR}/contracts/toyc-runtime-v1.json")
add_test(NAME sema_backend_c
         COMMAND python3 "${PROJECT_SOURCE_DIR}/tools/check_backend_c.py"
                 --coge $<TARGET_FILE:coge>
                 --source "${PROJECT_SOURCE_DIR}/coge/examples/toyc/toyc.coge"
                 --contracts "${PROJECT_SOURCE_DIR}/contracts/toyc-runtime-v1.json")
add_test(NAME sema_generated_modules
         COMMAND python3 "${PROJECT_SOURCE_DIR}/tools/check_generated_modules.py"
                 --coge $<TARGET_FILE:coge>
                 --source "${PROJECT_SOURCE_DIR}/coge/examples/toyc/toyc.coge"
                 --contracts "${PROJECT_SOURCE_DIR}/contracts/toyc-runtime-v1.json")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sema PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME sema_parses_minimal
         COMMAND coge --legacy "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/minimal.sema")
set_tests_properties(sema_parses_minimal PROPERTIES
    PASS_REGULAR_EXPRESSION "syntax OK")
add_test(NAME sema_parses_functions
         COMMAND coge --legacy "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/functions.sema")
set_tests_properties(sema_parses_functions PROPERTIES
    PASS_REGULAR_EXPRESSION "syntax OK")
add_test(NAME sema_parses_execution_model
         COMMAND coge --legacy "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/execution_model_syntax.sema")
set_tests_properties(sema_parses_execution_model PROPERTIES
    PASS_REGULAR_EXPRESSION "syntax OK")
add_test(NAME sema_rejects_function_cycle
         COMMAND coge --legacy "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/function_cycle.sema")
set_tests_properties(sema_rejects_function_cycle PROPERTIES WILL_FAIL TRUE)
add_test(NAME sema_rejects_unresolved_function_call
         COMMAND coge --legacy "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/function_unresolved.sema")
set_tests_properties(sema_rejects_unresolved_function_call PROPERTIES WILL_FAIL TRUE)
add_test(NAME sema_parses_legacy
         COMMAND coge --legacy
                 "${PROJECT_SOURCE_DIR}/sema/toyc.sema.txt"
                 "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/toyscope_1.sema.txt"
                 "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/toyscope_2.sema.txt"
                 "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/toyscope_3.sema.txt")
set_tests_properties(sema_parses_legacy PROPERTIES
    PASS_REGULAR_EXPRESSION "syntax OK")
add_test(NAME sema_rejects_invalid_syntax
         COMMAND coge --legacy "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/invalid.sema")
set_tests_properties(sema_rejects_invalid_syntax PROPERTIES WILL_FAIL TRUE)
add_test(NAME sema_collects_calls
         COMMAND coge --legacy --calls "${CMAKE_CURRENT_BINARY_DIR}/sema-call-names-test.txt"
                 "${PROJECT_SOURCE_DIR}/sema/toyc.sema.txt"
                 "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/toyscope_1.sema.txt"
                 "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/toyscope_2.sema.txt"
                 "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/toyscope_3.sema.txt")
set_tests_properties(sema_collects_calls PROPERTIES
    PASS_REGULAR_EXPRESSION "93 unique call names")
add_test(NAME sema_scope_codegen
    COMMAND "${CMAKE_COMMAND}"
        "-DSEMA=$<TARGET_FILE:coge>"
        "-DAGAS=$<TARGET_FILE:agas>"
        "-DSOURCE=${PROJECT_SOURCE_DIR}/sema/examples/scope_codegen/scope_codegen.sema"
        "-DGRAMMAR=${PROJECT_SOURCE_DIR}/sema/examples/scope_codegen/scope_codegen.ag"
        "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/scope-generated-test"
        "-DEXPECTED_DIR=${PROJECT_SOURCE_DIR}/sema/examples/scope_codegen/generated"
        "-DPARSER_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/scope-parser-generated-test.rs"
        "-DPARSER_EXPECTED=${PROJECT_SOURCE_DIR}/sema/examples/scope_codegen/generated/parser_gen.rs"
        -P "${PROJECT_SOURCE_DIR}/tools/check_scope_codegen.cmake")
add_test(NAME sema_scope_codegen_rejects_bad_type
    COMMAND "${CMAKE_COMMAND}"
        "-DSEMA=$<TARGET_FILE:coge>"
        "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/scope_codegen_bad_type.sema"
        "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/scope-invalid-test"
        "-DEXPECTED_MESSAGE=return type mismatch"
        -P "${PROJECT_SOURCE_DIR}/tools/check_scope_rejection.cmake")
add_test(NAME sema_scope_codegen_rejects_grammar_mismatch
    COMMAND "${CMAKE_COMMAND}"
        "-DSEMA=$<TARGET_FILE:coge>"
        "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/scope_codegen_bad_grammar.sema"
        "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/scope-grammar-mismatch-test"
        "-DEXPECTED_MESSAGE=rule program alternative 1 differs from source grammar in element count"
        -P "${PROJECT_SOURCE_DIR}/tools/check_scope_rejection.cmake")
add_test(NAME sema_inline_dispatch
    COMMAND python3
        "${PROJECT_SOURCE_DIR}/tools/check_inline_dispatch.py"
        --sema $<TARGET_FILE:coge>
        --agas $<TARGET_FILE:agas>
        --source-dir "${PROJECT_SOURCE_DIR}/sema/examples"
        --output-dir "${CMAKE_CURRENT_BINARY_DIR}/inline-dispatch-generated-test")
add_test(NAME sema_analyzer_inference
    COMMAND python3
        "${PROJECT_SOURCE_DIR}/tools/check_analyzer_inference.py"
        --sema $<TARGET_FILE:coge>
        --source-dir "${PROJECT_SOURCE_DIR}/sema/examples"
        --output-dir "${CMAKE_CURRENT_BINARY_DIR}/analyzer-inference-test")
add_test(NAME sema_typed_codegen
    COMMAND python3
        "${PROJECT_SOURCE_DIR}/tools/check_typed_codegen.py"
        --sema $<TARGET_FILE:coge>
        --source "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/typed_codegen.sema"
        --expected "${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/typed_codegen/generated/sema_gen.rs"
        --output-dir "${CMAKE_CURRENT_BINARY_DIR}/typed-codegen-generated-test")
add_test(NAME sema_typed_codegen_rejects_missing_signature
    COMMAND "${CMAKE_COMMAND}"
        "-DSEMA=$<TARGET_FILE:coge>"
        "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/typed_codegen_missing_signature.sema"
        "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/typed-missing-signature-test"
        "-DEXPECTED_MESSAGE=missing analyzer signature for rule value"
        -P "${PROJECT_SOURCE_DIR}/tools/check_scope_rejection.cmake")
add_test(NAME sema_toyscope_1_typed_codegen
    COMMAND "${CMAKE_COMMAND}"
        "-DSEMA=$<TARGET_FILE:sema>"
        "-DAGAS=$<TARGET_FILE:agas>"
        "-DSOURCE=${PROJECT_SOURCE_DIR}/sema/examples/toyscope_1/toyscope_1.sema"
        "-DCONTRACTS=${PROJECT_SOURCE_DIR}/contracts/toyscope-runtime-v1.json"
        "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/toyscope-1-typed-generated-test"
        "-DEXPECTED_DIR=${PROJECT_SOURCE_DIR}/sema/examples/toyscope_1/generated"
        "-DPARSER_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/toyscope-1-typed-parser-test.rs"
        "-DPARSER_EXPECTED=${PROJECT_SOURCE_DIR}/sema/examples/toyscope_1/generated/parser_gen.rs"
        -P "${PROJECT_SOURCE_DIR}/tools/check_scope_codegen.cmake")
add_test(NAME sema_toyscope_2_typed_codegen
    COMMAND "${CMAKE_COMMAND}"
        "-DSEMA=$<TARGET_FILE:sema>"
        "-DAGAS=$<TARGET_FILE:agas>"
        "-DSOURCE=${PROJECT_SOURCE_DIR}/sema/examples/toyscope_2/toyscope_2.sema"
        "-DCONTRACTS=${PROJECT_SOURCE_DIR}/contracts/toyscope-runtime-v1.json"
        "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/toyscope-2-typed-generated-test"
        "-DEXPECTED_DIR=${PROJECT_SOURCE_DIR}/sema/examples/toyscope_2/generated"
        "-DPARSER_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/toyscope-2-typed-parser-test.rs"
        "-DPARSER_EXPECTED=${PROJECT_SOURCE_DIR}/sema/examples/toyscope_2/generated/parser_gen.rs"
        -P "${PROJECT_SOURCE_DIR}/tools/check_scope_codegen.cmake")
add_test(NAME sema_toyscope_3_typed_codegen
    COMMAND "${CMAKE_COMMAND}"
        "-DSEMA=$<TARGET_FILE:sema>"
        "-DAGAS=$<TARGET_FILE:agas>"
        "-DSOURCE=${PROJECT_SOURCE_DIR}/sema/examples/toyscope_3/toyscope_3.sema"
        "-DCONTRACTS=${PROJECT_SOURCE_DIR}/contracts/toyscope-runtime-v1.json"
        "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/toyscope-3-typed-generated-test"
        "-DEXPECTED_DIR=${PROJECT_SOURCE_DIR}/sema/examples/toyscope_3/generated"
        "-DPARSER_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/toyscope-3-typed-parser-test.rs"
        "-DPARSER_EXPECTED=${PROJECT_SOURCE_DIR}/sema/examples/toyscope_3/generated/parser_gen.rs"
        -P "${PROJECT_SOURCE_DIR}/tools/check_scope_codegen.cmake")
add_test(NAME sema_typed_mixed_codegen
    COMMAND "${CMAKE_COMMAND}"
        "-DSEMA=$<TARGET_FILE:coge>"
        "-DAGAS=$<TARGET_FILE:agas>"
        "-DSOURCE=${PROJECT_SOURCE_DIR}/sema/examples/typed_mixed_codegen/typed_mixed.sema"
        "-DGRAMMAR=${PROJECT_SOURCE_DIR}/sema/examples/typed_mixed_codegen/typed_mixed.ag"
        "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/typed-mixed-generated-test"
        "-DEXPECTED_DIR=${PROJECT_SOURCE_DIR}/sema/examples/typed_mixed_codegen/generated"
        "-DPARSER_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/typed-mixed-parser-test.rs"
        "-DPARSER_EXPECTED=${PROJECT_SOURCE_DIR}/sema/examples/typed_mixed_codegen/generated/parser_gen.rs"
        -P "${PROJECT_SOURCE_DIR}/tools/check_scope_codegen.cmake")
add_test(NAME sema_toyc_typed_codegen
    COMMAND "${CMAKE_COMMAND}"
        "-DSEMA=$<TARGET_FILE:coge>"
        "-DAGAS=$<TARGET_FILE:agas>"
        "-DSOURCE=${PROJECT_SOURCE_DIR}/coge/examples/toyc/toyc.coge"
        "-DCONTRACTS=${PROJECT_SOURCE_DIR}/contracts/toyc-runtime-v1.json"
        "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/toyc-typed-generated-test"
        "-DEXPECTED_DIR=${PROJECT_SOURCE_DIR}/coge/examples/toyc/generated"
        "-DPARSER_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/toyc-typed-parser-test.rs"
        "-DPARSER_EXPECTED=${PROJECT_SOURCE_DIR}/coge/examples/toyc/generated/parser_gen.rs"
        -P "${PROJECT_SOURCE_DIR}/tools/check_scope_codegen.cmake")
add_test(NAME sema_execution_rejects_invalid_effects
    COMMAND python3
        "${PROJECT_SOURCE_DIR}/tools/check_execution_rejection.py"
        --coge $<TARGET_FILE:coge>
        --source "${PROJECT_SOURCE_DIR}/coge/examples/toyc/toyc.coge"
        --contracts "${PROJECT_SOURCE_DIR}/contracts/toyc-runtime-v1.json"
        --output-dir "${CMAKE_CURRENT_BINARY_DIR}/execution-rejection-test")
add_test(NAME sema_toycp_typed_codegen
    COMMAND "${CMAKE_COMMAND}"
        "-DSEMA=$<TARGET_FILE:coge>"
        "-DAGAS=$<TARGET_FILE:agas>"
        "-DSOURCE=${PROJECT_SOURCE_DIR}/coge/examples/toycp/toycp.coge"
        "-DCONTRACTS=${PROJECT_SOURCE_DIR}/contracts/toycp-runtime-v1.json"
        "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/toycp-typed-generated-test"
        "-DEXPECTED_DIR=${PROJECT_SOURCE_DIR}/coge/examples/toycp/generated"
        "-DPARSER_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/toycp-typed-parser-test.rs"
        "-DPARSER_EXPECTED=${PROJECT_SOURCE_DIR}/coge/examples/toycp/generated/parser_gen.rs"
        -P "${PROJECT_SOURCE_DIR}/tools/check_scope_codegen.cmake")
add_test(NAME sema_toyscope_1_reports_missing_typed_contract
    COMMAND "${CMAKE_COMMAND}"
        "-DSEMA=$<TARGET_FILE:coge>"
        "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/toyscope_1.sema.txt"
        "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/toyscope-1-generated-test"
        "-DEXPECTED_MESSAGE=Rust generation needs rust_context declaration"
        -P "${PROJECT_SOURCE_DIR}/tools/check_scope_rejection.cmake")
add_test(NAME sema_scope_codegen_rejects_effect_violation
    COMMAND "${CMAKE_COMMAND}"
        "-DSEMA=$<TARGET_FILE:coge>"
        "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/scope_codegen_bad_effect.sema"
        "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/scope-effect-violation-test"
        "-DEXPECTED_MESSAGE=mutating call from read-only function"
        -P "${PROJECT_SOURCE_DIR}/tools/check_scope_rejection.cmake")
add_test(NAME sema_scope_codegen_rejects_bad_analysis_result
    COMMAND "${CMAKE_COMMAND}"
        "-DSEMA=$<TARGET_FILE:coge>"
        "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/integration/fixtures/legacy/scope_codegen_bad_analysis.sema"
        "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/scope-bad-analysis-test"
        "-DEXPECTED_MESSAGE=analyzer result must have type SymbolId"
        -P "${PROJECT_SOURCE_DIR}/tools/check_scope_rejection.cmake")
add_test(NAME agas_emits_toycm_package
         COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_toycm_package.py"
                 --agas $<TARGET_FILE:agas>
                 --grammar "${PROJECT_SOURCE_DIR}/ag/grammars/examples/toycm.ag")
set_tests_properties(agas_emits_toycm_package PROPERTIES
    PASS_REGULAR_EXPRESSION "sections=7")
add_test(NAME agas_xml_example
         COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_xml_example.py"
                 --agas $<TARGET_FILE:agas>
                 --grammar "${PROJECT_SOURCE_DIR}/ag/grammars/examples/xml.ag"
                 --example "${PROJECT_SOURCE_DIR}/ag/grammars/examples/example.xml")
add_test(NAME agas_toycp_table
         COMMAND agas --table
                 "${PROJECT_SOURCE_DIR}/ag/grammars/examples/toycp.ag")
set_tests_properties(agas_toycp_table PROPERTIES
    PASS_REGULAR_EXPRESSION "conflicts=0")
add_test(NAME agas_parses_toycp_example
         COMMAND agas --diagnose-parse
                 "${PROJECT_SOURCE_DIR}/ag/grammars/examples/toycp.ag"
                 "${PROJECT_SOURCE_DIR}/ag/grammars/examples/toycp_example.toycp")
set_tests_properties(agas_parses_toycp_example PROPERTIES
    PASS_REGULAR_EXPRESSION "status=accepted")
add_test(NAME agas_help COMMAND agas --help)
set_tests_properties(agas_help PROPERTIES
    PASS_REGULAR_EXPRESSION "Agas - validate an Agas grammar")

if(AGAS_BUILD_ANTLR_BOOTSTRAP)
find_package(Java REQUIRED COMPONENTS Runtime)
find_package(antlr4-runtime 4.10 EXACT CONFIG REQUIRED)

set(AGAS_ANTLR_VERSION "4.10")
set(AGAS_ANTLR_JAR "" CACHE FILEPATH "Path to the ANTLR 4.10 complete JAR")

if(NOT AGAS_ANTLR_JAR)
    set(AGAS_ANTLR_JAR_DIR "${CMAKE_CURRENT_BINARY_DIR}/antlr4")
    set(AGAS_ANTLR_JAR "${AGAS_ANTLR_JAR_DIR}/antlr-${AGAS_ANTLR_VERSION}-complete.jar")
    file(MAKE_DIRECTORY "${AGAS_ANTLR_JAR_DIR}")

    if(NOT EXISTS "${AGAS_ANTLR_JAR}")
        message(STATUS "Downloading the official ANTLR ${AGAS_ANTLR_VERSION} bootstrap tool")
        file(DOWNLOAD
            "https://www.antlr.org/download/antlr-${AGAS_ANTLR_VERSION}-complete.jar"
            "${AGAS_ANTLR_JAR}"
            EXPECTED_HASH SHA256=fb5cf682be74aafb6cf355b118bb5b2fa4a10abd4bc0a59fb7883f597ecc7201
            STATUS AGAS_ANTLR_DOWNLOAD_STATUS
            TLS_VERIFY ON
            TIMEOUT 120
        )
        list(GET AGAS_ANTLR_DOWNLOAD_STATUS 0 AGAS_ANTLR_DOWNLOAD_CODE)
        if(NOT AGAS_ANTLR_DOWNLOAD_CODE EQUAL 0)
            list(GET AGAS_ANTLR_DOWNLOAD_STATUS 1 AGAS_ANTLR_DOWNLOAD_MESSAGE)
            message(FATAL_ERROR "Cannot download ANTLR: ${AGAS_ANTLR_DOWNLOAD_MESSAGE}")
        endif()
    endif()
endif()

if(NOT EXISTS "${AGAS_ANTLR_JAR}")
    message(FATAL_ERROR "ANTLR JAR does not exist: ${AGAS_ANTLR_JAR}")
endif()

file(SHA256 "${AGAS_ANTLR_JAR}" AGAS_ANTLR_JAR_SHA256)
if(NOT AGAS_ANTLR_JAR_SHA256 STREQUAL
   "fb5cf682be74aafb6cf355b118bb5b2fa4a10abd4bc0a59fb7883f597ecc7201")
    message(FATAL_ERROR "AGAS_ANTLR_JAR is not the pinned official ANTLR 4.10 JAR")
endif()

set(AGAS_GRAMMAR "${PROJECT_SOURCE_DIR}/ag/grammars/Ag.g4")
set(AGAS_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/bootstrap")
file(MAKE_DIRECTORY "${AGAS_GENERATED_DIR}")

set(AGAS_GENERATED_SOURCES
    "${AGAS_GENERATED_DIR}/AgLexer.cpp"
    "${AGAS_GENERATED_DIR}/AgParser.cpp"
    "${AGAS_GENERATED_DIR}/AgBaseVisitor.cpp"
    "${AGAS_GENERATED_DIR}/AgVisitor.cpp"
)

set(AGAS_GENERATED_HEADERS
    "${AGAS_GENERATED_DIR}/AgLexer.h"
    "${AGAS_GENERATED_DIR}/AgParser.h"
    "${AGAS_GENERATED_DIR}/AgBaseVisitor.h"
    "${AGAS_GENERATED_DIR}/AgVisitor.h"
)

add_custom_command(
    OUTPUT ${AGAS_GENERATED_SOURCES} ${AGAS_GENERATED_HEADERS}
    COMMAND "${Java_JAVA_EXECUTABLE}" -jar "${AGAS_ANTLR_JAR}"
            -Dlanguage=Cpp -visitor -no-listener
            -o "${AGAS_GENERATED_DIR}" Ag.g4
    DEPENDS "${AGAS_GRAMMAR}" "${AGAS_ANTLR_JAR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/grammars"
    COMMENT "Generating the temporary Agas bootstrap parser with ANTLR ${AGAS_ANTLR_VERSION}"
    VERBATIM
)

add_library(agas_bootstrap
    src/bootstrap/AntlrFrontend.cpp
    ${AGAS_GENERATED_SOURCES}
    ${AGAS_GENERATED_HEADERS}
)

target_include_directories(agas_bootstrap
    PUBLIC "${PROJECT_SOURCE_DIR}/ag/include"
    PRIVATE "${AGAS_GENERATED_DIR}"
)
target_include_directories(agas_bootstrap SYSTEM PRIVATE "${ANTLR4_INCLUDE_DIR}")

target_link_libraries(agas_bootstrap PUBLIC agas_core antlr4_shared)

add_executable(agas-bootstrap src/main.cpp)
target_link_libraries(agas-bootstrap PRIVATE agas_bootstrap agas_cli_support)
add_test(NAME agas_bootstrap_help COMMAND agas-bootstrap --help)
set_tests_properties(agas_bootstrap_help PROPERTIES
    PASS_REGULAR_EXPRESSION "Agas ANTLR bootstrap")

add_executable(agas-merge-report src/merge_report.cpp)
target_link_libraries(agas-merge-report PRIVATE agas_bootstrap agas_cli_support)
add_test(NAME agas_merge_report_help COMMAND agas-merge-report --help)
set_tests_properties(agas_merge_report_help PROPERTIES
    PASS_REGULAR_EXPRESSION "Agas merge report")
target_compile_definitions(agas-merge-report PRIVATE
    AGAS_DEFAULT_GRAMMAR_FILE="${PROJECT_SOURCE_DIR}/ag/grammars/Ag.ag"
)

target_compile_definitions(agas-bootstrap PRIVATE
    AGAS_DEFAULT_GRAMMAR_FILE="${PROJECT_SOURCE_DIR}/ag/grammars/Ag.ag"
    AGAS_GENERATOR_VERSION="${PROJECT_VERSION}"
    AGAS_ZBIK_REVISION="${AGAS_ZBIK_REVISION}"
)

set(AGAS_REPRODUCED_PACKAGE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/reproduced-bootstrap-ag-v1")
set(AGAS_REPRODUCED_RUST_PARSER
    "${CMAKE_CURRENT_BINARY_DIR}/reproduced-compressed-table.rs")
add_custom_target(agas_reproduce_from_antlr
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${AGAS_REPRODUCED_PACKAGE_DIR}"
    COMMAND $<TARGET_FILE:agas-bootstrap> --emit-package
            "${AGAS_REPRODUCED_PACKAGE_DIR}"
            "${PROJECT_SOURCE_DIR}/ag/grammars/Ag.ag"
    COMMAND $<TARGET_FILE:agas> --emit-rust-parser
            "${AGAS_REPRODUCED_RUST_PARSER}"
            "${PROJECT_SOURCE_DIR}/ag/grammars/CompressedTable.ag"
            "${AGAS_REPRODUCED_PACKAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E compare_files
            "${AGAS_REPRODUCED_RUST_PARSER}"
            "${PROJECT_SOURCE_DIR}/rust/crates/ag-runtime/src/generated/compressed_table.rs"
    COMMAND "${CMAKE_COMMAND}" "-DREPRODUCED_DIR=${AGAS_REPRODUCED_PACKAGE_DIR}"
            "-DPINNED_DIR=${CMAKE_CURRENT_SOURCE_DIR}/artifacts/ag/v1"
            -P "${PROJECT_SOURCE_DIR}/ag/cmake/CompareArtifactPackage.cmake"
    DEPENDS agas-bootstrap agas
    COMMENT "Reproducing pinned Agas artifacts from the ANTLR bootstrap"
    VERBATIM
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agas_bootstrap PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(agas-bootstrap PRIVATE -Wall -Wextra -Wpedantic)
endif()

endif() # AGAS_BUILD_ANTLR_BOOTSTRAP

add_subdirectory(ag/tests)

add_executable(agsem_document_projection_tests "${PROJECT_SOURCE_DIR}/tests/integration/DocumentProjectionTest.cpp")
target_include_directories(agsem_document_projection_tests PRIVATE "${PROJECT_SOURCE_DIR}/sema/src")
target_link_libraries(agsem_document_projection_tests PRIVATE coge_core)
add_dependencies(agsem_document_projection_tests sema_document_artifact sema_artifact)
add_test(NAME agsem_document_projection_tests COMMAND agsem_document_projection_tests
    "${SEMA_DOCUMENT_ARTIFACT_DIR}" "${PROJECT_SOURCE_DIR}/ag/artifacts/ag/v1"
    "${SEMA_ARTIFACT_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_BINARY_DIR}/stage3-fixtures")
add_executable(agsem_semantic_link_tests "${PROJECT_SOURCE_DIR}/tests/integration/SemanticLinkTest.cpp")
target_link_libraries(agsem_semantic_link_tests PRIVATE agsem_core)
add_dependencies(agsem_semantic_link_tests sema_document_artifact)
add_test(NAME agsem_semantic_link_tests COMMAND agsem_semantic_link_tests
    "${SEMA_DOCUMENT_ARTIFACT_DIR}" "${PROJECT_SOURCE_DIR}/ag/artifacts/ag/v1"
    "${PROJECT_SOURCE_DIR}/coge/examples/toyc/generated/projections/toyc.sema"
    "${PROJECT_SOURCE_DIR}/contracts/toyc-runtime-v1.json")
add_test(NAME agsem_template_tests COMMAND python3
    "${PROJECT_SOURCE_DIR}/tests/integration/check_templates.py"
    --sema $<TARGET_FILE:sema> --coge $<TARGET_FILE:coge>
    --repo "${CMAKE_CURRENT_SOURCE_DIR}/..")
set_tests_properties(agsem_template_tests PROPERTIES TIMEOUT 180)
add_test(NAME agsem_document_cli_tests COMMAND python3
    "${PROJECT_SOURCE_DIR}/tests/integration/check_document_cli.py"
    --sema $<TARGET_FILE:sema> --coge $<TARGET_FILE:coge>
    --ag $<TARGET_FILE:ag> --agas $<TARGET_FILE:agas>
    --fixtures "${CMAKE_CURRENT_BINARY_DIR}/stage3-fixtures" --repo "${CMAKE_CURRENT_SOURCE_DIR}/..")
set_tests_properties(agsem_document_cli_tests PROPERTIES DEPENDS agsem_document_projection_tests)
add_test(NAME agsem_toyscope_layer_tests COMMAND python3
    "${PROJECT_SOURCE_DIR}/tests/integration/check_toyscope_layers.py"
    --sema $<TARGET_FILE:sema> --coge $<TARGET_FILE:coge> --ag $<TARGET_FILE:ag>
    --repo "${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME agsem_migrated_examples_tests COMMAND python3
    "${PROJECT_SOURCE_DIR}/tests/integration/check_migrated_examples.py"
    --sema $<TARGET_FILE:sema> --coge $<TARGET_FILE:coge> --ag $<TARGET_FILE:ag>
    --repo "${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME agsem_migration_message_tests COMMAND python3
    "${PROJECT_SOURCE_DIR}/tests/integration/check_migration_messages.py"
    --sema $<TARGET_FILE:sema> --coge $<TARGET_FILE:coge>
    --repo "${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME agsem_enum_constant_tests COMMAND python3
    "${PROJECT_SOURCE_DIR}/tests/integration/check_enum_constants.py"
    --sema $<TARGET_FILE:sema> --coge $<TARGET_FILE:coge>
    --repo "${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME agsem_collection_tests COMMAND python3
    "${PROJECT_SOURCE_DIR}/tests/integration/check_collections.py"
    --sema $<TARGET_FILE:sema> --coge $<TARGET_FILE:coge>
    --repo "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_executable(agsem_completeness_tests "${PROJECT_SOURCE_DIR}/tests/integration/CompletenessTest.cpp")
target_link_libraries(agsem_completeness_tests PRIVATE coge_core)
add_dependencies(agsem_completeness_tests sema_document_artifact)
add_test(NAME agsem_completeness_tests COMMAND agsem_completeness_tests
    "${SEMA_DOCUMENT_ARTIFACT_DIR}" "${PROJECT_SOURCE_DIR}/ag/artifacts/ag/v1"
    "${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME agsem_completeness_cli_tests COMMAND python3
    "${PROJECT_SOURCE_DIR}/tests/integration/check_completeness_cli.py"
    --sema $<TARGET_FILE:sema> --coge $<TARGET_FILE:coge>)
set_tests_properties(agsem_completeness_cli_tests PROPERTIES TIMEOUT 90)

add_executable(agsem_stage6_acceptance_tests "${PROJECT_SOURCE_DIR}/tests/integration/Stage6AcceptanceTest.cpp")
target_link_libraries(agsem_stage6_acceptance_tests PRIVATE agsem_core)
add_dependencies(agsem_stage6_acceptance_tests sema_document_artifact)
add_test(NAME agsem_stage6_acceptance_tests COMMAND agsem_stage6_acceptance_tests
    "${SEMA_DOCUMENT_ARTIFACT_DIR}" "${PROJECT_SOURCE_DIR}/ag/artifacts/ag/v1")

add_custom_target(agsem_stage6_tests DEPENDS
    ag agas sema coge
    agsem_checked_semantics_tests agsem_prepared_function_plan_tests
    agsem_semantic_binding_tests agsem_document_model_tests
    agsem_document_frontend_tests coge_execution_profile_model_tests
    agsem_document_projection_tests agsem_semantic_link_tests
    agsem_completeness_tests agsem_stage6_acceptance_tests)
set_tests_properties(
    agsem_checked_semantics_tests agsem_prepared_function_plan_tests
    agsem_semantic_binding_tests agsem_document_model_tests
    agsem_document_frontend_tests coge_execution_profile_model_tests
    coge_execution_profiles agsem_document_projection_tests
    agsem_semantic_link_tests agsem_template_tests agsem_document_cli_tests
    agsem_toyscope_layer_tests agsem_migrated_examples_tests
    agsem_completeness_tests agsem_completeness_cli_tests
    agsem_stage6_acceptance_tests
    PROPERTIES LABELS "agsem_stage6")
