add_library(ziliu_core STATIC
  src/candidate_filter.cpp
  src/ipc_protocol.cpp
  src/settings.cpp
  src/session_host.cpp
  src/sogou_theme.cpp
  src/stub_engine.cpp
  src/theme_manifest.cpp)

target_include_directories(ziliu_core PUBLIC include)
ziliu_configure_target(ziliu_core)
