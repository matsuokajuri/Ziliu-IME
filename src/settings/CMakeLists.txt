add_executable(ZiliuSettings WIN32 src/settings_main.cpp)
target_link_libraries(ZiliuSettings PRIVATE d2d1 dwrite dwmapi shell32)
ziliu_configure_target(ZiliuSettings)

