add_executable(ZiliuRegister register_main.cpp)
target_include_directories(ZiliuRegister PRIVATE "${PROJECT_SOURCE_DIR}/src/tsf/include")
target_link_libraries(ZiliuRegister PRIVATE ole32 uuid advapi32)
if(MSVC)
  target_link_options(
    ZiliuRegister PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()
ziliu_configure_target(ZiliuRegister)
