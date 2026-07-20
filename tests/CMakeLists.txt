add_executable(ziliu_core_tests core_tests.cpp)
target_link_libraries(ziliu_core_tests PRIVATE ziliu_core)
ziliu_configure_target(ziliu_core_tests)

add_test(NAME ziliu_core_tests COMMAND ziliu_core_tests)

