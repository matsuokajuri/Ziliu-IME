add_executable(ZiliuBroker WIN32 src/broker_main.cpp)
target_link_libraries(ZiliuBroker PRIVATE user32)
ziliu_configure_target(ZiliuBroker)

