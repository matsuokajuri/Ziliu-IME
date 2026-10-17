# Explicit optional broker-worker dependency. Never link this target to ZiliuTIP.
function(ziliu_add_onnx_scorer)
  set(ZILIU_ORT_INCLUDE_DIR "" CACHE PATH "Verified ONNX Runtime 1.20.1 C API header directory")
  if(NOT EXISTS "${ZILIU_ORT_INCLUDE_DIR}/onnxruntime_c_api.h")
    message(FATAL_ERROR "Set ZILIU_ORT_INCLUDE_DIR to an existing verified ORT 1.20.1 header directory.")
  endif()
  add_library(ziliu_onnx_scorer STATIC
    "${ZILIU_SOURCE_ROOT}/src/broker/src/onnx_scorer.cpp"
    "${ZILIU_SOURCE_ROOT}/src/broker/src/scorer_assets.cpp"
    "${ZILIU_SOURCE_ROOT}/src/broker/src/scorer_platform.cpp"
    "${ZILIU_SOURCE_ROOT}/src/broker/src/onnx_graph_gate.cpp")
  target_include_directories(ziliu_onnx_scorer PUBLIC "${ZILIU_SOURCE_ROOT}/src/broker/include")
  target_include_directories(ziliu_onnx_scorer SYSTEM PRIVATE "${ZILIU_ORT_INCLUDE_DIR}")
  target_link_libraries(ziliu_onnx_scorer PUBLIC ziliu_core)
  if(WIN32)
    target_link_libraries(ziliu_onnx_scorer PRIVATE bcrypt psapi)
  else()
    find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
    target_link_libraries(ziliu_onnx_scorer PRIVATE OpenSSL::Crypto ${CMAKE_DL_LIBS})
  endif()
  ziliu_configure_target(ziliu_onnx_scorer)
endfunction()
