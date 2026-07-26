cmake_minimum_required(VERSION 3.21)
project(Nevarea VERSION 0.1.0 LANGUAGES CXX C)

get_filename_component(NEVAREA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

option(NEVAREA_BUILD_DYNAMIC "Build Nevarea as a shared library (DLL/.so)" ON)
option(NEVAREA_BUILD_SAMPLES "Build the sample programs" ON)
option(NEVAREA_BUILD_TESTS "Build the headless test suite" OFF)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_subdirectory(${NEVAREA_ROOT}/Nevarea ${CMAKE_BINARY_DIR}/Nevarea)

if(NEVAREA_BUILD_SAMPLES)
  add_subdirectory(${NEVAREA_ROOT}/Samples ${CMAKE_BINARY_DIR}/Samples)
endif()

if(NEVAREA_BUILD_TESTS)
  add_subdirectory(${NEVAREA_ROOT}/tests ${CMAKE_BINARY_DIR}/tests)
endif()