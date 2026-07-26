# UniLume project
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.16)

project(
  UniLume
  VERSION 0.1.0
  DESCRIPTION "Bộ gõ tiếng Việt hiện đại, nhẹ và nhanh cho Linux, phát triển trực tiếp từ lõi UniKey."
  LANGUAGES CXX
)

include(GNUInstallDirs)
include(CTest)

include(cmake/UniLumeVersion.cmake)

option(UNILUME_ENABLE_ASAN "Enable AddressSanitizer for UniLume targets" OFF)
option(UNILUME_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer for UniLume targets" OFF)
option(UNILUME_BUILD_BENCHMARKS "Build UniLume core benchmarks" OFF)
option(UNILUME_BUILD_INTEGRATION_BENCHMARKS
  "Build UniLume adapter integration benchmarks" OFF)
option(UNILUME_BUILD_FCITX5_ADDON "Build the experimental Fcitx5 addon" OFF)

add_library(unilume_sanitizer_options INTERFACE)

if(UNILUME_ENABLE_ASAN OR UNILUME_ENABLE_UBSAN)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR
      "UniLume sanitizers require a GCC- or Clang-compatible compiler"
    )
  endif()

  set(_unilume_sanitizers)
  if(UNILUME_ENABLE_ASAN)
    list(APPEND _unilume_sanitizers address)
  endif()
  if(UNILUME_ENABLE_UBSAN)
    list(APPEND _unilume_sanitizers undefined)
  endif()
  list(JOIN _unilume_sanitizers "," _unilume_sanitizer_list)

  target_compile_options(unilume_sanitizer_options INTERFACE
    "-fsanitize=${_unilume_sanitizer_list}"
    -fno-omit-frame-pointer
  )
  target_link_options(unilume_sanitizer_options INTERFACE
    "-fsanitize=${_unilume_sanitizer_list}"
  )
endif()

add_library(unilume_engine STATIC
  src/byteio/byteio.cpp
  src/vnconv/charset.cpp
  src/vnconv/convert.cpp
  src/vnconv/data.cpp
  src/vnconv/error.cpp
  src/vnconv/pattern.cpp
  src/ukengine/inputproc.cpp
  src/ukengine/mactab.cpp
  src/ukengine/ukengine.cpp
  src/ukengine/usrkeymap.cpp
  src/ukinterface/unikey.cpp
)

target_link_libraries(unilume_engine PRIVATE unilume_sanitizer_options)

target_include_directories(unilume_engine
  PUBLIC
    src/ukinterface
    src/ukengine
  PRIVATE
    src/byteio
    src/vnconv
)

# The inherited tables use 8-bit character constants and predate C++11.
# Keeping the original language dialect avoids changing their data or behavior.
set_target_properties(unilume_engine PROPERTIES
  CXX_STANDARD 98
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS YES
  POSITION_INDEPENDENT_CODE ON
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(unilume_engine PRIVATE
    -Wall
    -Wextra
    -Wno-narrowing
    # One value is retained for a disabled legacy code path in ukengine.cpp.
    -Wno-unused-but-set-variable
    -Wno-write-strings
  )
endif()

add_library(unilume_context STATIC
  src/ukinterface/unilume_context.cpp
)

target_link_libraries(unilume_context PUBLIC
  unilume_engine
  unilume_sanitizer_options
)

target_include_directories(unilume_context
  PUBLIC
    src/ukinterface
  PRIVATE
    src/byteio
    src/ukengine
    src/vnconv
)

set_target_properties(unilume_context PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
  POSITION_INDEPENDENT_CODE ON
)

add_library(unilume_config STATIC
  src/config/config_snapshot.cpp
  src/config/config_store.cpp
)

target_link_libraries(unilume_config PRIVATE unilume_sanitizer_options)

target_include_directories(unilume_config PUBLIC src/config)

set_target_properties(unilume_config PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
  POSITION_INDEPENDENT_CODE ON
)

add_library(unilume_core STATIC
  src/core/direct_commit_controller.cpp
  src/core/engine_context.cpp
  src/core/preedit_fallback_controller.cpp
  src/core/replacement_transaction.cpp
  src/core/utf8_validation.cpp
  src/platform/input_mode_policy.cpp
)

target_link_libraries(unilume_core PUBLIC
  unilume_context
  unilume_sanitizer_options
)

target_include_directories(unilume_core PUBLIC
  src/core
  src/platform
)

set_target_properties(unilume_core PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
  POSITION_INDEPENDENT_CODE ON
)

if(BUILD_TESTING OR UNILUME_BUILD_INTEGRATION_BENCHMARKS)
  add_library(unilume_integration_simulation STATIC
    src/platform/simulation/deterministic_backend.cpp
  )

  target_link_libraries(unilume_integration_simulation PUBLIC
    unilume_core
    unilume_sanitizer_options
  )

  target_include_directories(unilume_integration_simulation PUBLIC
    src/platform/simulation
  )

  set_target_properties(unilume_integration_simulation PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

if(UNILUME_BUILD_BENCHMARKS OR UNILUME_BUILD_INTEGRATION_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(UNILUME_BUILD_FCITX5_ADDON)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FCITX5_CORE QUIET IMPORTED_TARGET "Fcitx5Core>=5.0")
  pkg_check_modules(FCITX5_CONFIG QUIET IMPORTED_TARGET "Fcitx5Config>=5.0")
  if(NOT FCITX5_CORE_FOUND OR NOT FCITX5_CONFIG_FOUND)
    message(FATAL_ERROR
      "UNILUME_BUILD_FCITX5_ADDON=ON requires Fcitx5Core and Fcitx5Config development files"
    )
  endif()
  add_subdirectory(src/fcitx5)
endif()

# ---- Install docs ----
install(
  FILES
    LICENSE
    README.md
    AUTHORS.md
    NOTICE
    CONTRIBUTING.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/unilume
)

# ---- Install documentation directory ----
install(
  DIRECTORY docs/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/unilume
  FILES_MATCHING
    PATTERN "*.md"
  REGEX "/legacy/" EXCLUDE
)

# ---- Install license (also in standard location) ----
install(
  FILES LICENSE
  DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/unilume
  RENAME copyright
)
