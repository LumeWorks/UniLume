# UniLume project
# SPDX-License-Identifier: GPL-2.0-or-later

include(GNUInstallDirs)
include(CheckCXXSourceCompiles)

set(CMAKE_REQUIRED_INCLUDES ${FCITX5_CORE_INCLUDE_DIRS})
set(_unilume_saved_try_compile_target_type ${CMAKE_TRY_COMPILE_TARGET_TYPE})
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
check_cxx_source_compiles([[#include <fcitx/inputcontext.h>
int main() {
  fcitx::AtomicSurroundingTextInputContext *context = nullptr;
  return context != nullptr;
}
]] UNILUME_HAVE_FCITX_ATOMIC_REPLACEMENT)
set(CMAKE_TRY_COMPILE_TARGET_TYPE ${_unilume_saved_try_compile_target_type})
unset(_unilume_saved_try_compile_target_type)
unset(CMAKE_REQUIRED_INCLUDES)

add_library(unilume_fcitx5 MODULE
  acknowledged_backspace_transaction.cpp
  addon.cpp
  diagnostic_trace.cpp
  emoji_picker.cpp
  fcitx_key_mapper.cpp
  fcitx_replacement_backend.cpp
  input_context_state.cpp
  input_method_config.cpp
  replacement_transport_contract.cpp
  status_action_model.cpp
  uinput_backspace_device.cpp
  verified_surrounding_snapshot.cpp
)

target_link_libraries(unilume_fcitx5 PRIVATE
  PkgConfig::FCITX5_CORE
  PkgConfig::FCITX5_CONFIG
  PkgConfig::FCITX5_MODULE
  unilume_core
  unilume_dictionary
  unilume_emoji
  unilume_keymap
  unilume_macro
  unilume_sanitizer_options
)

target_include_directories(unilume_fcitx5 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(unilume_fcitx5 PRIVATE
  FCITX_GETTEXT_DOMAIN="unilume"
  UNILUME_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}"
  UNILUME_DIAGNOSTIC_UNILUME_VERSION="${PROJECT_VERSION}"
  UNILUME_DIAGNOSTIC_FCITX_VERSION="${FCITX5_CORE_VERSION}"
  UNILUME_HAVE_FCITX_ATOMIC_REPLACEMENT=$<BOOL:${UNILUME_HAVE_FCITX_ATOMIC_REPLACEMENT}>
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
configure_file(
  ${PROJECT_SOURCE_DIR}/data/org.fcitx.Fcitx5.Addon.UniLume.metainfo.xml.in
  ${CMAKE_CURRENT_BINARY_DIR}/org.fcitx.Fcitx5.Addon.UniLume.metainfo.xml
  @ONLY
)

set(UNILUME_VI_CATALOG
  ${CMAKE_CURRENT_BINARY_DIR}/locale/vi/LC_MESSAGES/unilume.mo
)
add_custom_command(
  OUTPUT ${UNILUME_VI_CATALOG}
  COMMAND ${CMAKE_COMMAND} -E make_directory
    ${CMAKE_CURRENT_BINARY_DIR}/locale/vi/LC_MESSAGES
  COMMAND ${GETTEXT_MSGFMT_EXECUTABLE}
    --check
    --check-format
    -o ${UNILUME_VI_CATALOG}
    ${PROJECT_SOURCE_DIR}/po/vi.po
  DEPENDS ${PROJECT_SOURCE_DIR}/po/vi.po
  VERBATIM
)
add_custom_target(unilume-translations ALL DEPENDS ${UNILUME_VI_CATALOG})

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
install(FILES ${UNILUME_VI_CATALOG}
  DESTINATION ${CMAKE_INSTALL_LOCALEDIR}/vi/LC_MESSAGES
)
install(FILES
  ${PROJECT_SOURCE_DIR}/data/icons/unilume.svg
  ${PROJECT_SOURCE_DIR}/data/icons/unilume-off.svg
  ${PROJECT_SOURCE_DIR}/data/icons/unilume-fallback.svg
  DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)
install(FILES
  ${PROJECT_SOURCE_DIR}/data/org.fcitx.Fcitx5.Addon.UniLume.desktop
  DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/org.fcitx.Fcitx5.Addon.UniLume.metainfo.xml
  DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
)
install(FILES
  ${PROJECT_SOURCE_DIR}/data/70-unilume-uinput.rules
  DESTINATION lib/udev/rules.d
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

  add_executable(unilume_fcitx5_status_action_tests
    input_method_config.cpp
    status_action_model.cpp
    status_action_model_test.cpp
  )
  target_link_libraries(unilume_fcitx5_status_action_tests PRIVATE
    PkgConfig::FCITX5_CONFIG
    unilume_core
    unilume_sanitizer_options
  )
  target_include_directories(unilume_fcitx5_status_action_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_compile_definitions(unilume_fcitx5_status_action_tests PRIVATE
    FCITX_GETTEXT_DOMAIN="unilume"
  )
  set_target_properties(unilume_fcitx5_status_action_tests PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )
  add_test(
    NAME fcitx5-status-actions
    COMMAND unilume_fcitx5_status_action_tests
  )

  add_executable(unilume_fcitx5_translation_tests translation_test.cpp)
  target_link_libraries(unilume_fcitx5_translation_tests PRIVATE
    PkgConfig::FCITX5_CORE
    unilume_sanitizer_options
  )
  target_compile_definitions(unilume_fcitx5_translation_tests PRIVATE
    FCITX_GETTEXT_DOMAIN="unilume"
    UNILUME_TEST_LOCALE_DIR="${CMAKE_CURRENT_BINARY_DIR}/locale"
  )
  set_target_properties(unilume_fcitx5_translation_tests PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )
  add_dependencies(
    unilume_fcitx5_translation_tests
    unilume-translations
  )
  add_test(
    NAME fcitx5-translation
    COMMAND unilume_fcitx5_translation_tests
  )
  set_tests_properties(fcitx5-translation PROPERTIES
    ENVIRONMENT "LANGUAGE=vi;LC_ALL=C.UTF-8"
  )

  add_executable(unilume_fcitx5_emoji_module_tests
    emoji_module_test.cpp
  )
  target_link_libraries(unilume_fcitx5_emoji_module_tests PRIVATE
    PkgConfig::FCITX5_CORE
    PkgConfig::FCITX5_MODULE
    unilume_core
    unilume_emoji
    unilume_sanitizer_options
  )
  target_include_directories(unilume_fcitx5_emoji_module_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/core
  )
  target_compile_definitions(unilume_fcitx5_emoji_module_tests PRIVATE
    UNILUME_TEST_FCITX_VERSION="${FCITX5_CORE_VERSION}"
  )
  set_target_properties(unilume_fcitx5_emoji_module_tests PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )
  add_test(
    NAME fcitx5-emoji-module
    COMMAND unilume_fcitx5_emoji_module_tests
  )

  add_executable(unilume_fcitx5_backend_contract_tests
    replacement_transport_contract.cpp
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

  add_executable(unilume_fcitx5_acknowledged_backspace_tests
    acknowledged_backspace_transaction.cpp
    acknowledged_backspace_transaction_test.cpp
  )
  target_link_libraries(
    unilume_fcitx5_acknowledged_backspace_tests PRIVATE
    unilume_sanitizer_options
  )
  target_include_directories(
    unilume_fcitx5_acknowledged_backspace_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  set_target_properties(
    unilume_fcitx5_acknowledged_backspace_tests PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )
  add_test(
    NAME fcitx5-acknowledged-backspace
    COMMAND unilume_fcitx5_acknowledged_backspace_tests
  )

  add_executable(unilume_fcitx5_key_mapper_tests
    fcitx_key_mapper.cpp
    fcitx_key_mapper_test.cpp
  )
  target_link_libraries(unilume_fcitx5_key_mapper_tests PRIVATE
    PkgConfig::FCITX5_CORE
    unilume_core
    unilume_sanitizer_options
  )
  target_include_directories(unilume_fcitx5_key_mapper_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  set_target_properties(unilume_fcitx5_key_mapper_tests PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )
  add_test(
    NAME fcitx5-key-mapper
    COMMAND unilume_fcitx5_key_mapper_tests
  )

  add_executable(unilume_fcitx5_diagnostic_tests
    diagnostic_trace.cpp
    diagnostic_trace_test.cpp
  )
  target_link_libraries(unilume_fcitx5_diagnostic_tests PRIVATE
    PkgConfig::FCITX5_CORE
    unilume_core
    unilume_sanitizer_options
  )
  target_include_directories(unilume_fcitx5_diagnostic_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_compile_definitions(unilume_fcitx5_diagnostic_tests PRIVATE
    UNILUME_DIAGNOSTIC_UNILUME_VERSION="${PROJECT_VERSION}"
    UNILUME_DIAGNOSTIC_FCITX_VERSION="${FCITX5_CORE_VERSION}"
  )
  set_target_properties(unilume_fcitx5_diagnostic_tests PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )
  add_test(
    NAME fcitx5-diagnostics
    COMMAND unilume_fcitx5_diagnostic_tests
  )
endif()
