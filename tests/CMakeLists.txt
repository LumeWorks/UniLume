# UniLume project
# SPDX-License-Identifier: GPL-2.0-or-later

add_executable(unilume_engine_tests
  engine/composition_span_counterexample_tests.cpp
  engine/editing_tests.cpp
  engine/engine_test_main.cpp
  engine/input_tests.cpp
  engine/telex_tests.cpp
  engine/test_support/engine_fixture.cpp
  engine/test_support/test_context.cpp
  engine/unicode_invalid_input_tests.cpp
  engine/unicode_passthrough_tests.cpp
  engine/url_and_code_input_tests.cpp
  engine/vni_tests.cpp
  engine/viqr_tests.cpp
)

target_link_libraries(unilume_engine_tests PRIVATE
  unilume_engine
  unilume_sanitizer_options
)

target_include_directories(unilume_engine_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/engine
)

target_include_directories(unilume_engine_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/engine
)

set_target_properties(unilume_engine_tests PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

add_test(NAME engine COMMAND unilume_engine_tests)

add_executable(unilume_legacy_safety_tests
  engine/legacy_safety_tests.cpp
)
target_link_libraries(unilume_legacy_safety_tests PRIVATE
  unilume_engine
  unilume_sanitizer_options
)
target_include_directories(unilume_legacy_safety_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src/byteio
  ${CMAKE_SOURCE_DIR}/src/vnconv
)
set_target_properties(unilume_legacy_safety_tests PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)
add_test(NAME legacy-safety COMMAND unilume_legacy_safety_tests)

add_executable(unilume_engine_allocation_failure_tests
  engine/allocation_failure_tests.cpp
)
target_link_libraries(unilume_engine_allocation_failure_tests PRIVATE
  unilume_context
  unilume_sanitizer_options
)
set_target_properties(unilume_engine_allocation_failure_tests PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)
add_test(
  NAME engine-allocation-failure
  COMMAND unilume_engine_allocation_failure_tests
)

add_executable(unilume_config_tests
  config/config_tests.cpp
)

target_link_libraries(unilume_config_tests PRIVATE
  unilume_config
  unilume_sanitizer_options
)

target_compile_definitions(unilume_config_tests PRIVATE
  UNILUME_CONFIG_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config/fixtures"
)

set_target_properties(unilume_config_tests PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

add_test(NAME config COMMAND unilume_config_tests)

add_executable(unilume_macro_tests
  macro/macro_tests.cpp
)

target_link_libraries(unilume_macro_tests PRIVATE
  unilume_macro
  unilume_context
  unilume_sanitizer_options
)

set_target_properties(unilume_macro_tests PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

add_test(NAME macro COMMAND unilume_macro_tests)

add_executable(unilume_keymap_tests
  keymap/keymap_tests.cpp
)

target_link_libraries(unilume_keymap_tests PRIVATE
  unilume_context
  unilume_keymap
  unilume_sanitizer_options
)

set_target_properties(unilume_keymap_tests PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

add_test(NAME keymap COMMAND unilume_keymap_tests)

add_executable(unilume_dictionary_tests
  dictionary/dictionary_tests.cpp
)

target_link_libraries(unilume_dictionary_tests PRIVATE
  unilume_core
  unilume_dictionary
  unilume_sanitizer_options
)

set_target_properties(unilume_dictionary_tests PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

add_test(NAME dictionary COMMAND unilume_dictionary_tests)

add_executable(unilume_emoji_tests
  emoji/emoji_model_tests.cpp
)
target_link_libraries(unilume_emoji_tests PRIVATE
  unilume_emoji
  unilume_sanitizer_options
)
set_target_properties(unilume_emoji_tests PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)
add_test(NAME emoji COMMAND unilume_emoji_tests)

add_executable(unilume_application_policy_tests
  policy/application_policy_tests.cpp
)
target_link_libraries(unilume_application_policy_tests PRIVATE
  unilume_application_policy
  unilume_sanitizer_options
)
set_target_properties(unilume_application_policy_tests PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)
add_test(NAME application-policy COMMAND unilume_application_policy_tests)

add_executable(unilume_adaptive_router_tests
  platform/adaptive_router_tests.cpp
)
target_link_libraries(unilume_adaptive_router_tests PRIVATE
  unilume_core
  unilume_sanitizer_options
)
set_target_properties(unilume_adaptive_router_tests PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)
add_test(NAME adaptive-router COMMAND unilume_adaptive_router_tests)

add_executable(unilume_capability_builder_tests
  platform/capability_builder_tests.cpp
)
target_link_libraries(unilume_capability_builder_tests PRIVATE
  unilume_core
  unilume_sanitizer_options
)
set_target_properties(unilume_capability_builder_tests PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)
add_test(NAME capability-builder COMMAND unilume_capability_builder_tests)

add_executable(unilume_adaptive_routing_pipeline_tests
  platform/adaptive_routing_pipeline_tests.cpp
  ../src/fcitx5/developer_route_override.cpp
)
target_link_libraries(unilume_adaptive_routing_pipeline_tests PRIVATE
  unilume_core
  unilume_sanitizer_options
)
target_include_directories(unilume_adaptive_routing_pipeline_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src/fcitx5
)
set_target_properties(unilume_adaptive_routing_pipeline_tests PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)
add_test(NAME adaptive-routing-pipeline
         COMMAND unilume_adaptive_routing_pipeline_tests)

add_executable(unilume_property_tests
  ../fuzz/property_test_main.cpp
  ../fuzz/support/fuzz_harness.cpp
)

target_link_libraries(unilume_property_tests PRIVATE
  unilume_config
  unilume_core
  unilume_integration_simulation
  unilume_macro
  unilume_sanitizer_options
)

target_include_directories(unilume_property_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/fuzz/support
)

target_compile_definitions(unilume_property_tests PRIVATE
  UNILUME_FUZZ_CORPUS_DIR="${CMAKE_SOURCE_DIR}/fuzz/corpus"
)

set_target_properties(unilume_property_tests PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

add_test(NAME property-model COMMAND unilume_property_tests)

add_library(unilume_integration_test_support STATIC
  integration/integration_fixture.cpp
  integration/test_assertions.cpp
)

target_link_libraries(unilume_integration_test_support PUBLIC
  unilume_core
  unilume_integration_simulation
  unilume_sanitizer_options
)

target_include_directories(unilume_integration_test_support PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/integration
)

set_target_properties(unilume_integration_test_support PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

add_executable(unilume_integration_tests
  integration/browser_capability_tests.cpp
  integration/browser_input_session_tests.cpp
  integration/burst_tests.cpp
  integration/delayed_surrounding_text_tests.cpp
  integration/dictionary_tests.cpp
  integration/duplicate_text_tests.cpp
  integration/immediate_tests.cpp
  integration/input_mode_policy_tests.cpp
  integration/macro_tests.cpp
  integration/options_tests.cpp
  integration/integration_test_main.cpp
  integration/preedit_fallback_tests.cpp
  integration/split_transport_backend.cpp
  integration/split_transport_tests.cpp
  integration/stability_recovery_tests.cpp
  integration/transaction_tests.cpp
  integration/typing_pipeline_tests.cpp
  integration/zero_preedit_architecture_tests.cpp
)

target_link_libraries(unilume_integration_tests PRIVATE
  unilume_integration_test_support
  unilume_sanitizer_options
)

set_target_properties(unilume_integration_tests PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

foreach(suite IN ITEMS
    immediate
    delayed
    duplicate
    transaction
    typing-pipeline
    preedit-fallback
    browser-capability
    browser-input-session
    mode-policy
    options
    macro
    dictionary
    burst
    soak-smoke
    stability-recovery
    zero-preedit-architecture
    zero-preedit-soak
    outcome-containment
)
  add_test(
    NAME "integration-${suite}"
    COMMAND unilume_integration_tests "${suite}"
  )
endforeach()
