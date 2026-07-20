add_library(ziliu_core STATIC
  src/ipc_protocol.cpp
  src/session_host.cpp
  src/stub_engine.cpp)

target_include_directories(ziliu_core PUBLIC include)
ziliu_configure_target(ziliu_core)
