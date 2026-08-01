# UniLume project
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.16)

project(
  UniLume
  VERSION 0.1.1
  DESCRIPTION "Bộ gõ tiếng Việt hiện đại, nhẹ và nhanh cho Linux, phát triển trực tiếp từ lõi UniKey."
  LANGUAGES CXX
)

include(GNUInstallDirs)
include(CTest)

include(cmake/UniLumeVersion.cmake)

option(UNILUME_ENABLE_ASAN "Enable AddressSanitizer for UniLume targets" OFF)
option(UNILUME_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer for UniLume targets" OFF)
option(UNILUME_ENABLE_TSAN "Enable ThreadSanitizer for UniLume targets" OFF)
option(UNILUME_BUILD_BENCHMARKS "Build UniLume core benchmarks" OFF)
option(UNILUME_BUILD_INTEGRATION_BENCHMARKS
  "Build UniLume adapter integration benchmarks" OFF)
option(UNILUME_BUILD_FCITX5_ADDON "Build the experimental Fcitx5 addon" OFF)
option(UNILUME_BUILD_CONFIG_GUI
  "Build the Qt6 production configuration GUI" OFF)
option(UNILUME_BUILD_FUZZERS "Build Clang libFuzzer targets" OFF)

add_library(unilume_sanitizer_options INTERFACE)

if(UNILUME_ENABLE_ASAN AND UNILUME_ENABLE_TSAN)
  message(FATAL_ERROR
    "AddressSanitizer and ThreadSanitizer cannot be enabled together"
  )
endif()

if(UNILUME_ENABLE_ASAN OR UNILUME_ENABLE_UBSAN OR UNILUME_ENABLE_TSAN)
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
  if(UNILUME_ENABLE_TSAN)
    list(APPEND _unilume_sanitizers thread)
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

add_library(unilume_macro STATIC
  src/macro/macro_contract.cpp
  src/macro/macro_store.cpp
)

add_library(unilume_keymap STATIC
  src/keymap/keymap_contract.cpp
)

add_library(unilume_dictionary STATIC
  src/dictionary/dictionary_contract.cpp
  src/dictionary/dictionary_store.cpp
)

add_library(unilume_application_policy STATIC
  src/policy/application_policy.cpp
)

add_library(unilume_emoji STATIC
  src/emoji/emoji_model.cpp
)

target_link_libraries(unilume_emoji PUBLIC
  unilume_core
  unilume_sanitizer_options
)
target_include_directories(unilume_emoji
  PUBLIC src/emoji
  PRIVATE src/core
)
set_target_properties(unilume_emoji PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
  POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(unilume_application_policy PRIVATE
  unilume_sanitizer_options
)
target_include_directories(unilume_application_policy PUBLIC src/policy)
set_target_properties(unilume_application_policy PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
  POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(unilume_dictionary PRIVATE unilume_sanitizer_options)
target_include_directories(unilume_dictionary PUBLIC src/dictionary)
set_target_properties(unilume_dictionary PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
  POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(unilume_keymap PRIVATE unilume_sanitizer_options)
target_include_directories(unilume_keymap PUBLIC src/keymap)
set_target_properties(unilume_keymap PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
  POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(unilume_macro PUBLIC
  unilume_engine
  unilume_sanitizer_options
)

target_include_directories(unilume_macro
  PUBLIC src/macro
  PRIVATE src/vnconv
)

set_target_properties(unilume_macro PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
  POSITION_INDEPENDENT_CODE ON
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
  src/core/engine_options.cpp
  src/core/preedit_fallback_controller.cpp
  src/core/replacement_transaction.cpp
  src/core/typing_pipeline.cpp
  src/core/utf8_validation.cpp
  src/platform/input_mode_policy.cpp
)

target_link_libraries(unilume_core PUBLIC
  unilume_application_policy
  unilume_config
  unilume_context
  unilume_dictionary
  unilume_keymap
  unilume_macro
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

if(BUILD_TESTING OR UNILUME_BUILD_INTEGRATION_BENCHMARKS OR UNILUME_BUILD_FUZZERS)
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

if(UNILUME_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "UNILUME_BUILD_FUZZERS requires Clang")
  endif()
  add_subdirectory(fuzz)
endif()

if(UNILUME_BUILD_BENCHMARKS OR UNILUME_BUILD_INTEGRATION_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(UNILUME_BUILD_FCITX5_ADDON OR UNILUME_BUILD_CONFIG_GUI)
  find_package(PkgConfig REQUIRED)
  find_package(Gettext REQUIRED)
  pkg_check_modules(FCITX5_CORE QUIET IMPORTED_TARGET "Fcitx5Core>=5.0")
  pkg_check_modules(FCITX5_CONFIG QUIET IMPORTED_TARGET "Fcitx5Config>=5.0")
endif()

if(UNILUME_BUILD_FCITX5_ADDON)
  pkg_check_modules(FCITX5_MODULE QUIET IMPORTED_TARGET "Fcitx5Module>=5.0")
  if(NOT FCITX5_CORE_FOUND OR NOT FCITX5_CONFIG_FOUND OR
     NOT FCITX5_MODULE_FOUND)
    message(FATAL_ERROR
      "UNILUME_BUILD_FCITX5_ADDON=ON requires Fcitx5Core, Fcitx5Config and Fcitx5Module development files"
    )
  endif()
  add_subdirectory(src/fcitx5)
endif()

if(UNILUME_BUILD_CONFIG_GUI)
  if(NOT UNILUME_BUILD_FCITX5_ADDON)
    message(FATAL_ERROR
      "UNILUME_BUILD_CONFIG_GUI=ON requires UNILUME_BUILD_FCITX5_ADDON=ON"
    )
  endif()
  find_package(Qt6 6.4 REQUIRED COMPONENTS Core DBus Widgets)
  find_package(Fcitx5Qt6DBusAddons 5.0 REQUIRED)
  target_compile_definitions(unilume_fcitx5 PRIVATE
    UNILUME_HAS_CONFIG_GUI=1
  )
  add_subdirectory(src/config_gui)
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
