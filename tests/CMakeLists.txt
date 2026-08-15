add_executable(ziliu_core_tests core_tests.cpp)
target_link_libraries(ziliu_core_tests PRIVATE ziliu_core)
ziliu_configure_target(ziliu_core_tests)

add_test(NAME ziliu_core_tests COMMAND ziliu_core_tests)

add_executable(ziliu_theme_manifest_tests theme_manifest_tests.cpp)
target_link_libraries(ziliu_theme_manifest_tests PRIVATE ziliu_core)
ziliu_configure_target(ziliu_theme_manifest_tests)

add_test(NAME ziliu_theme_manifest_tests COMMAND ziliu_theme_manifest_tests)

add_executable(ziliu_sogou_theme_tests sogou_theme_tests.cpp)
target_link_libraries(ziliu_sogou_theme_tests PRIVATE ziliu_core)
if(WIN32)
  target_sources(ziliu_sogou_theme_tests PRIVATE
    ../src/settings/sogou_ssf_container.cpp)
  target_link_libraries(ziliu_sogou_theme_tests PRIVATE bcrypt)
endif()
ziliu_configure_target(ziliu_sogou_theme_tests)

add_test(NAME ziliu_sogou_theme_tests COMMAND ziliu_sogou_theme_tests)

if(TARGET ZiliuTextRaster)
  add_executable(ziliu_text_raster_math_tests text_raster_math_tests.cpp)
  target_include_directories(
    ziliu_text_raster_math_tests
    PRIVATE ../src/text-raster/src)
  ziliu_configure_target(ziliu_text_raster_math_tests)

  add_test(
    NAME ziliu_text_raster_math_tests
    COMMAND ziliu_text_raster_math_tests)

  add_executable(ziliu_text_raster_api_tests text_raster_api_tests.cpp)
  target_link_libraries(ziliu_text_raster_api_tests PRIVATE ZiliuTextRaster)
  ziliu_configure_target(ziliu_text_raster_api_tests)

  add_test(
    NAME ziliu_text_raster_api_tests
    COMMAND ziliu_text_raster_api_tests)
endif()

add_executable(
  ziliu_ui_coordinate_space_tests
  ui_coordinate_space_tests.cpp)
ziliu_configure_target(ziliu_ui_coordinate_space_tests)

add_test(
  NAME ziliu_ui_coordinate_space_tests
  COMMAND ziliu_ui_coordinate_space_tests)

add_executable(
  ziliu_ui_sogou_surface_scaler_tests
  ui_sogou_surface_scaler_tests.cpp)
ziliu_configure_target(ziliu_ui_sogou_surface_scaler_tests)

add_test(
  NAME ziliu_ui_sogou_surface_scaler_tests
  COMMAND ziliu_ui_sogou_surface_scaler_tests)

if(WIN32)
  add_executable(ziliu_sogou_ssf_container_tests
    sogou_ssf_container_tests.cpp
    ../src/settings/sogou_ssf_container.cpp)
  target_link_libraries(ziliu_sogou_ssf_container_tests PRIVATE bcrypt ziliu_core)
  ziliu_configure_target(ziliu_sogou_ssf_container_tests)

  add_test(
    NAME ziliu_sogou_ssf_container_tests
    COMMAND ziliu_sogou_ssf_container_tests)
endif()

add_executable(ziliu_ipc_tests ipc_tests.cpp)
target_link_libraries(ziliu_ipc_tests PRIVATE ziliu_ipc_client ziliu_ipc_server)
ziliu_configure_target(ziliu_ipc_tests)

add_test(NAME ziliu_ipc_tests COMMAND ziliu_ipc_tests)
set_tests_properties(ziliu_ipc_tests PROPERTIES TIMEOUT 10)

if(ZILIU_ENABLE_RIME)
  add_executable(ziliu_rime_engine_tests rime_engine_tests.cpp)
  target_link_libraries(ziliu_rime_engine_tests PRIVATE ziliu_broker_engine)
  add_dependencies(ziliu_rime_engine_tests ZiliuBroker)
  ziliu_configure_target(ziliu_rime_engine_tests)

  add_test(NAME ziliu_rime_engine_tests COMMAND ziliu_rime_engine_tests)
  set_tests_properties(ziliu_rime_engine_tests PROPERTIES
    ENVIRONMENT "ZILIU_RIME_USER_DATA_DIR=${CMAKE_BINARY_DIR}/test-state/rime-v3"
    SKIP_RETURN_CODE 77
    TIMEOUT 120)
endif()
