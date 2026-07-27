cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED PROJECT_NAME)
  project(Nevarea VERSION 0.1.0 LANGUAGES CXX C)
endif()

include(FetchContent)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

FetchContent_Declare(VulkanHeaders
  GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
  GIT_TAG v1.4.350)
FetchContent_MakeAvailable(VulkanHeaders)

FetchContent_Declare(VulkanUtilityLibraries
  GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Utility-Libraries.git
  GIT_TAG v1.4.350)
FetchContent_MakeAvailable(VulkanUtilityLibraries)

add_library(VulkanMemoryAllocator INTERFACE)
target_include_directories(VulkanMemoryAllocator SYSTEM INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/vma/)

if(NEVAREA_BUILD_DYNAMIC)
  set(NEVAREA_LIB_TYPE SHARED)
else()
  set(NEVAREA_LIB_TYPE STATIC)
endif()

add_library(Nevarea ${NEVAREA_LIB_TYPE}
  src/Core/Log.cpp
  src/Platform/WindowSystem.cpp
  src/Renderer/Rendering.cpp
  src/Renderer/Vulkan/VulkanContext.cpp
  src/Renderer/Vulkan/VulkanDevice.cpp
  src/Renderer/Vulkan/VulkanSwapchain.cpp
  src/Renderer/Vulkan/VulkanFrames.cpp
  src/Renderer/Vulkan/VulkanAllocator.cpp
  src/Renderer/Vulkan/VulkanResourceManager.cpp
  src/Renderer/Vulkan/VulkanPipeline.cpp
  src/Renderer/Vulkan/VulkanDebug.cpp
  src/Renderer/Vulkan/VulkanTranslate.cpp
  src/Renderer/Vulkan/vulkan_interop.cpp
  vendor/volk/volk.c)
add_library(Nevarea::Nevarea ALIAS Nevarea)

target_precompile_headers(Nevarea PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/Core/n_pch.hpp>")

target_include_directories(Nevarea
  PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
          $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_include_directories(Nevarea SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/volk/)

target_link_libraries(Nevarea PRIVATE
  Vulkan::Headers Vulkan::UtilityHeaders VulkanMemoryAllocator)

set_target_properties(Nevarea PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED ON
  C_STANDARD 11
  EXPORT_NAME Nevarea
  OUTPUT_NAME Nevarea)

target_compile_definitions(Nevarea PUBLIC
  $<$<CONFIG:Debug,RelWithDebInfo>:NEVAREA_DEBUG>)

if(NEVAREA_BUILD_DYNAMIC)
  target_compile_definitions(Nevarea PUBLIC NEVAREA_BUILD_DLL)
  target_compile_definitions(Nevarea PRIVATE NEVAREA_RENDERER_EXPORT)
endif()

if(WIN32)
  target_compile_definitions(Nevarea PRIVATE VK_USE_PLATFORM_WIN32_KHR)
elseif(APPLE)
  target_compile_definitions(Nevarea PRIVATE VK_USE_PLATFORM_METAL_EXT)
elseif(UNIX)
  target_compile_definitions(Nevarea PRIVATE VK_USE_PLATFORM_XLIB_KHR)
endif()

if(MSVC)
  target_compile_options(Nevarea PRIVATE /W4 /permissive- /FS)
else()
  target_compile_options(Nevarea PRIVATE -Wall -Wextra -pedantic)
endif()

install(TARGETS Nevarea EXPORT NevareaTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT NevareaTargets
  NAMESPACE Nevarea:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Nevarea)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/NevareaConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/NevareaConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Nevarea)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/NevareaConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/NevareaConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/NevareaConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Nevarea)

if(NEVAREA_BUILD_DYNAMIC AND MSVC)
  install(FILES $<TARGET_PDB_FILE:Nevarea>
    DESTINATION ${CMAKE_INSTALL_BINDIR} OPTIONAL)
endif()

if(CMAKE_EXPORT_COMPILE_COMMANDS)
  file(CREATE_LINK
    "${CMAKE_BINARY_DIR}/compile_commands.json"
    "${NEVAREA_ROOT}/compile_commands.json"
    SYMBOLIC COPY_ON_ERROR)
endif()
