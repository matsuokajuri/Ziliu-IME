add_library(ziliu_broker_engine STATIC src/rime_engine.cpp)
target_include_directories(ziliu_broker_engine PUBLIC include)
if(ZILIU_ENABLE_RIME)
  set(ZILIU_RIME_API_HEADER
      "${PROJECT_SOURCE_DIR}/third_party/librime/src/rime_api.h")
  if(NOT EXISTS "${ZILIU_RIME_API_HEADER}")
    message(FATAL_ERROR
      "ZILIU_ENABLE_RIME=ON requires the initialized librime submodule: "
      "${ZILIU_RIME_API_HEADER}")
  endif()
  target_include_directories(ziliu_broker_engine SYSTEM PRIVATE
    "${PROJECT_SOURCE_DIR}/third_party/librime/src")
  target_compile_definitions(ziliu_broker_engine PRIVATE ZILIU_ENABLE_RIME=1)
endif()
target_link_libraries(ziliu_broker_engine PUBLIC ziliu_core PRIVATE ole32 shell32)
ziliu_configure_target(ziliu_broker_engine)

add_executable(ZiliuBroker WIN32 src/broker_main.cpp)
target_link_libraries(ZiliuBroker PRIVATE ziliu_broker_engine ziliu_ipc_server)
ziliu_configure_target(ZiliuBroker)

if(ZILIU_ENABLE_RIME)
add_custom_target(ziliu_stage_rime_data ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ZiliuBroker>/data/rime"
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
          "${PROJECT_SOURCE_DIR}/third_party/rime-ice"
          "$<TARGET_FILE_DIR:ZiliuBroker>/data/rime"
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
          "${PROJECT_SOURCE_DIR}/data/ziliu"
          "$<TARGET_FILE_DIR:ZiliuBroker>/data/rime"
  COMMENT "Staging pinned Rime Ice data and Ziliu overlays")
add_dependencies(ZiliuBroker ziliu_stage_rime_data)

set(ZILIU_RIME_RUNTIME_DLL
    "${PROJECT_SOURCE_DIR}/.cache/librime-runtime/dist/lib/rime.dll")
if(EXISTS "${ZILIU_RIME_RUNTIME_DLL}")
  add_custom_target(ziliu_stage_rime_runtime ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ZILIU_RIME_RUNTIME_DLL}"
            "$<TARGET_FILE_DIR:ZiliuBroker>/rime.dll"
    COMMENT "Staging verified librime 1.17.0 runtime")
  add_dependencies(ZiliuBroker ziliu_stage_rime_runtime)
else()
  message(STATUS
    "Verified librime runtime not found; run scripts/fetch-librime-runtime.ps1. "
    "Broker will use the deterministic fallback engine.")
endif()
endif()
