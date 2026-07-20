add_custom_target(ZiliuSettings ALL
  COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/build-winui3.cmd"
          "$<CONFIG>"
          "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
  BYPRODUCTS "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/ZiliuSettings.exe"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Building WinUI 3 settings and quick menu"
  VERBATIM)
