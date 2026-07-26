# UniLume configuration GUI
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(unilume_config_gui_model STATIC
  backup_codec.cpp
  generation_store.cpp
  settings_model.cpp
  ${PROJECT_SOURCE_DIR}/src/fcitx5/input_method_config.cpp
)
target_link_libraries(unilume_config_gui_model PUBLIC
  PkgConfig::FCITX5_CONFIG
  unilume_application_policy
  unilume_core
  unilume_dictionary
  unilume_keymap
  unilume_macro
  unilume_sanitizer_options
)
target_include_directories(unilume_config_gui_model PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src/fcitx5
)
target_compile_definitions(unilume_config_gui_model PRIVATE
  UNILUME_HAS_CONFIG_GUI=1
)
set_target_properties(unilume_config_gui_model PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
  POSITION_INDEPENDENT_CODE ON
)

add_executable(unilume-config
  fcitx_config_client.cpp
  main.cpp
  main_window.cpp
  settings_editor.cpp
)
target_link_libraries(unilume-config PRIVATE
  Fcitx5Qt6::DBusAddons
  PkgConfig::FCITX5_CORE
  Qt6::DBus
  Qt6::Widgets
  unilume_config_gui_model
  unilume_sanitizer_options
)
target_compile_definitions(unilume-config PRIVATE
  FCITX_GETTEXT_DOMAIN="unilume"
  UNILUME_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}"
)
set_target_properties(unilume-config PROPERTIES
  AUTOMOC ON
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

install(TARGETS unilume-config
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES
  ${PROJECT_SOURCE_DIR}/data/org.unilume.UniLume.Configuration.desktop
  DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

if(BUILD_TESTING)
  add_executable(unilume_config_gui_model_tests
    ${PROJECT_SOURCE_DIR}/tests/config_gui/settings_model_tests.cpp
  )
  target_link_libraries(unilume_config_gui_model_tests PRIVATE
    unilume_config_gui_model
    unilume_sanitizer_options
  )
  set_target_properties(unilume_config_gui_model_tests PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )
  add_test(NAME config-gui-model COMMAND unilume_config_gui_model_tests)

  add_executable(unilume_config_gui_accessibility_tests
    settings_editor.cpp
    ${PROJECT_SOURCE_DIR}/tests/config_gui/settings_editor_tests.cpp
  )
  target_link_libraries(unilume_config_gui_accessibility_tests PRIVATE
    PkgConfig::FCITX5_CORE
    Qt6::Widgets
    unilume_config_gui_model
    unilume_sanitizer_options
  )
  target_compile_definitions(
    unilume_config_gui_accessibility_tests PRIVATE
    FCITX_GETTEXT_DOMAIN="unilume"
  )
  set_target_properties(unilume_config_gui_accessibility_tests PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )
  add_test(
    NAME config-gui-accessibility
    COMMAND unilume_config_gui_accessibility_tests
  )
  set_tests_properties(config-gui-accessibility PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  )
  add_test(
    NAME config-gui-hidpi
    COMMAND unilume_config_gui_accessibility_tests
  )
  set_tests_properties(config-gui-hidpi PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=2"
  )
endif()
