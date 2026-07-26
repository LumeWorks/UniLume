# UniLume project
# SPDX-License-Identifier: GPL-2.0-or-later

include(GNUInstallDirs)

add_library(unilume_fcitx5 MODULE
  addon.cpp
  diagnostic_trace.cpp
  fcitx_key_mapper.cpp
  fcitx_replacement_backend.cpp
  input_context_state.cpp
  input_method_config.cpp
  verified_surrounding_snapshot.cpp
)

target_link_libraries(unilume_fcitx5 PRIVATE
  PkgConfig::FCITX5_CORE
  PkgConfig::FCITX5_CONFIG
  unilume_core
  unilume_dictionary
  unilume_keymap
  unilume_macro
)

target_include_directories(unilume_fcitx5 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(unilume_fcitx5 PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
  PREFIX ""
  OUTPUT_NAME "unilume"
)

configure_file(
  unilume-addon.conf.in
  ${CMAKE_CURRENT_BINARY_DIR}/unilume-addon.conf
  @ONLY
)

install(TARGETS unilume_fcitx5
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/fcitx5
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/unilume-addon.conf
  DESTINATION ${CMAKE_INSTALL_DATADIR}/fcitx5/addon
  RENAME unilume.conf
)
install(FILES unilume-inputmethod.conf
  DESTINATION ${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod
  RENAME unilume.conf
)

if(BUILD_TESTING)
  add_executable(unilume_fcitx5_config_tests
    input_method_config.cpp
    input_method_config_test.cpp
  )
  target_link_libraries(unilume_fcitx5_config_tests PRIVATE
    PkgConfig::FCITX5_CONFIG
    unilume_core
    unilume_sanitizer_options
  )
  target_include_directories(unilume_fcitx5_config_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  set_target_properties(unilume_fcitx5_config_tests PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )
  add_test(NAME fcitx5-config COMMAND unilume_fcitx5_config_tests)

  add_executable(unilume_fcitx5_backend_contract_tests
    verified_surrounding_snapshot.cpp
    verified_surrounding_snapshot_test.cpp
  )
  target_link_libraries(unilume_fcitx5_backend_contract_tests PRIVATE
    PkgConfig::FCITX5_CORE
    unilume_core
    unilume_sanitizer_options
  )
  target_include_directories(unilume_fcitx5_backend_contract_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  set_target_properties(unilume_fcitx5_backend_contract_tests PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )
  add_test(
    NAME fcitx5-backend-contract
    COMMAND unilume_fcitx5_backend_contract_tests
  )
endif()
