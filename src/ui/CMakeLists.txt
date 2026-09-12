add_library(ziliu_ui STATIC
  src/candidate_window.cpp)

target_include_directories(ziliu_ui PUBLIC include)
target_link_libraries(ziliu_ui PUBLIC ziliu_core
  PRIVATE advapi32 d2d1 dwrite dwmapi ole32 windowscodecs)
ziliu_configure_target(ziliu_ui)
