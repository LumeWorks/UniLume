# SPDX-License-Identifier: GPL-2.0-or-later

add_library(unilume_fuzz_support STATIC
  support/fuzz_harness.cpp
)

target_link_libraries(unilume_fuzz_support PUBLIC
  unilume_config
  unilume_core
  unilume_integration_simulation
  unilume_macro
  unilume_sanitizer_options
)

target_include_directories(unilume_fuzz_support PUBLIC support)
target_compile_options(unilume_fuzz_support PRIVATE -fsanitize=fuzzer-no-link)

set_target_properties(unilume_fuzz_support PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

foreach(target IN ITEMS engine parsers transaction)
  add_executable(unilume_fuzz_${target} ${target}_fuzzer.cpp)
  target_link_libraries(unilume_fuzz_${target} PRIVATE unilume_fuzz_support)
  target_compile_options(unilume_fuzz_${target} PRIVATE -fsanitize=fuzzer)
  target_link_options(unilume_fuzz_${target} PRIVATE -fsanitize=fuzzer)
  set_target_properties(unilume_fuzz_${target} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )
endforeach()
