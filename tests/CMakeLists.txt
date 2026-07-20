add_executable(ziliu_core_tests core_tests.cpp)
target_link_libraries(ziliu_core_tests PRIVATE ziliu_core)
ziliu_configure_target(ziliu_core_tests)

add_test(NAME ziliu_core_tests COMMAND ziliu_core_tests)

add_executable(ziliu_ipc_tests ipc_tests.cpp)
target_link_libraries(ziliu_ipc_tests PRIVATE ziliu_ipc_client ziliu_ipc_server)
ziliu_configure_target(ziliu_ipc_tests)

add_test(NAME ziliu_ipc_tests COMMAND ziliu_ipc_tests)
set_tests_properties(ziliu_ipc_tests PROPERTIES TIMEOUT 10)

add_executable(ziliu_rime_engine_tests rime_engine_tests.cpp)
target_link_libraries(ziliu_rime_engine_tests PRIVATE ziliu_broker_engine)
add_dependencies(ziliu_rime_engine_tests ZiliuBroker)
ziliu_configure_target(ziliu_rime_engine_tests)

add_test(NAME ziliu_rime_engine_tests COMMAND ziliu_rime_engine_tests)
set_tests_properties(ziliu_rime_engine_tests PROPERTIES
  ENVIRONMENT "ZILIU_RIME_USER_DATA_DIR=${CMAKE_BINARY_DIR}/test-state/rime-v3"
  SKIP_RETURN_CODE 77
  TIMEOUT 120)
