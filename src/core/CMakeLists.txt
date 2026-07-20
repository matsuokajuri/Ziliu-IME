add_library(ziliu_core STATIC
  src/stub_engine.cpp)

target_include_directories(ziliu_core PUBLIC include)
ziliu_configure_target(ziliu_core)

