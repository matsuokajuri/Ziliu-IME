cmake_minimum_required(VERSION 3.28)

project(
  Ziliu
  VERSION 0.1.0
  DESCRIPTION "Ziliu - a clean, local-first Chinese input method for Windows"
  LANGUAGES CXX)

set(ZILIU_RELEASE_VERSION "0.1.0-alpha.1")
set(ZILIU_FILE_VERSION "0,1,0,1")

if(NOT WIN32)
  message(FATAL_ERROR "Ziliu is a Windows-only project.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  # A TIP is loaded into arbitrary client processes. Keep first-party binaries
  # independent of a separately installed Visual C++ Redistributable.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(ZILIU_BUILD_TESTS "Build Ziliu unit tests" ON)
option(ZILIU_ENABLE_RIME "Build the librime adapter when the dependency is available" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ZiliuWarnings)

function(ziliu_add_version_resource target description original_filename file_type)
  set(ZILIU_VERSION_DESCRIPTION "${description}")
  set(ZILIU_VERSION_ORIGINAL_FILENAME "${original_filename}")
  set(ZILIU_VERSION_FILE_TYPE "${file_type}")
  set(version_resource "${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc")
  configure_file("${PROJECT_SOURCE_DIR}/cmake/ZiliuVersion.rc.in" "${version_resource}" @ONLY)
  target_sources(${target} PRIVATE "${version_resource}")
endfunction()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

add_subdirectory(src/core)
add_subdirectory(src/ui)
add_subdirectory(src/tsf)
add_subdirectory(src/ipc)
add_subdirectory(src/broker)
add_subdirectory(src/settings)
add_subdirectory(tools/register)

if(ZILIU_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()
