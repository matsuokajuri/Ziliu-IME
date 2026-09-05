add_library(ziliu_bitmap STATIC src/bitmap.cpp)
target_include_directories(ziliu_bitmap PUBLIC include)
target_link_libraries(ziliu_bitmap PRIVATE ole32 shlwapi windowscodecs)
ziliu_configure_target(ziliu_bitmap)

add_library(ziliu_theme_background STATIC src/sogou_background.cpp)
target_link_libraries(ziliu_theme_background PUBLIC ziliu_core ziliu_bitmap)
ziliu_configure_target(ziliu_theme_background)

add_library(ziliu_ui STATIC
  src/candidate_window.cpp)

target_include_directories(ziliu_ui PUBLIC include)
target_link_libraries(ziliu_ui PUBLIC ziliu_core PRIVATE advapi32 d2d1 dwrite dwmapi)
ziliu_configure_target(ziliu_ui)
