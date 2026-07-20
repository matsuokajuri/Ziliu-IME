add_executable(ZiliuRegister register_main.cpp)
target_include_directories(ZiliuRegister PRIVATE "${PROJECT_SOURCE_DIR}/src/tsf/include")
target_link_libraries(ZiliuRegister PRIVATE ole32 uuid advapi32)
ziliu_configure_target(ZiliuRegister)
