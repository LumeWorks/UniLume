# UniLume project
# SPDX-License-Identifier: GPL-2.0-or-later

find_package(Git QUIET)
set(UNILUME_BENCHMARK_COMMIT "unknown")
if(Git_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE UNILUME_BENCHMARK_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
endif()

function(unilume_enable_allocation_wrapping target)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR
      "UniLume allocation instrumentation requires GCC or Clang"
    )
  endif()
  target_link_options(${target} PRIVATE
    -Wl,--wrap=malloc
    -Wl,--wrap=calloc
    -Wl,--wrap=realloc
    -Wl,--wrap=free
  )
endfunction()

if(UNILUME_BUILD_BENCHMARKS)
  add_library(unilume_allocation_instrumentation STATIC
    core/allocation_instrumentation.cpp
  )
  target_include_directories(unilume_allocation_instrumentation PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core
  )
  target_link_libraries(unilume_allocation_instrumentation PRIVATE
    unilume_sanitizer_options
  )
  set_target_properties(unilume_allocation_instrumentation PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )

  add_executable(unilume_core_benchmark
    core/allocation_benchmark.cpp
    core/benchmark_options.cpp
    core/burst_benchmark.cpp
    core/checksum.cpp
    core/correctness.cpp
    core/corpus_loader.cpp
    core/engine_fixture.cpp
    core/latency_benchmark.cpp
    core/main.cpp
    core/report_writer.cpp
    core/rss_reader.cpp
    core/soak_benchmark.cpp
    core/statistics.cpp
    core/system_metadata.cpp
    core/throughput_benchmark.cpp
  )

  target_link_libraries(unilume_core_benchmark PRIVATE
    unilume_engine
    unilume_allocation_instrumentation
    unilume_sanitizer_options
  )
  unilume_enable_allocation_wrapping(unilume_core_benchmark)

  target_include_directories(unilume_core_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core
  )

  target_compile_definitions(unilume_core_benchmark PRIVATE
    UNILUME_BENCHMARK_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    UNILUME_BENCHMARK_COMMIT="${UNILUME_BENCHMARK_COMMIT}"
    UNILUME_BENCHMARK_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    UNILUME_BENCHMARK_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus"
  )

  set_target_properties(unilume_core_benchmark PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(unilume_core_benchmark PRIVATE -Wall -Wextra)
  endif()

  add_executable(unilume_allocation_instrumentation_test
    core/allocation_instrumentation_test.cpp
  )
  target_link_libraries(unilume_allocation_instrumentation_test PRIVATE
    unilume_allocation_instrumentation
    unilume_sanitizer_options
  )
  unilume_enable_allocation_wrapping(unilume_allocation_instrumentation_test)
  set_target_properties(unilume_allocation_instrumentation_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    INTERPROCEDURAL_OPTIMIZATION FALSE
  )
  target_compile_options(unilume_allocation_instrumentation_test PRIVATE
    -fno-builtin-malloc
    -fno-builtin-calloc
    -fno-builtin-realloc
  )

  add_executable(unilume_dictionary_benchmark
    dictionary/dictionary_benchmark.cpp
  )
  target_link_libraries(unilume_dictionary_benchmark PRIVATE
    unilume_dictionary
    unilume_allocation_instrumentation
    unilume_sanitizer_options
  )
  target_include_directories(unilume_dictionary_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core
  )
  unilume_enable_allocation_wrapping(unilume_dictionary_benchmark)
  set_target_properties(unilume_dictionary_benchmark PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )

  if(BUILD_TESTING)
    add_test(NAME benchmark-smoke COMMAND unilume_core_benchmark --smoke)
    set_tests_properties(benchmark-smoke PROPERTIES TIMEOUT 60)
    add_test(
      NAME allocation-instrumentation
      COMMAND unilume_allocation_instrumentation_test
    )
    set_tests_properties(allocation-instrumentation PROPERTIES TIMEOUT 30)
    add_test(
      NAME dictionary-benchmark
      COMMAND unilume_dictionary_benchmark
    )
    set_tests_properties(dictionary-benchmark PROPERTIES TIMEOUT 30)
  endif()
endif()

if(UNILUME_BUILD_INTEGRATION_BENCHMARKS)
  add_executable(unilume_integration_benchmark
    core/checksum.cpp
    core/process_resource_reader.cpp
    core/rss_reader.cpp
    core/statistics.cpp
    core/system_metadata.cpp
    integration/integration_benchmark_options.cpp
    integration/integration_benchmark_runner.cpp
    integration/integration_report_writer.cpp
    integration/integration_session.cpp
    integration/main.cpp
  )

  target_link_libraries(unilume_integration_benchmark PRIVATE
    unilume_integration_simulation
    unilume_sanitizer_options
  )

  target_include_directories(unilume_integration_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/integration
  )

  target_compile_definitions(unilume_integration_benchmark PRIVATE
    UNILUME_BENCHMARK_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    UNILUME_BENCHMARK_COMMIT="${UNILUME_BENCHMARK_COMMIT}"
    UNILUME_BENCHMARK_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
  )

  set_target_properties(unilume_integration_benchmark PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(unilume_integration_benchmark PRIVATE -Wall -Wextra)
  endif()

  if(BUILD_TESTING)
    add_test(
      NAME integration-benchmark-smoke
      COMMAND unilume_integration_benchmark --smoke
    )
    add_test(
      NAME integration-benchmark-typing-smoke
      COMMAND unilume_integration_benchmark --smoke --typing=enabled
    )
    set_tests_properties(
      integration-benchmark-smoke
      integration-benchmark-typing-smoke
      PROPERTIES TIMEOUT 60
    )
  endif()
endif()
