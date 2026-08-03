add_executable(hello_triangle main.cpp)
target_link_libraries(hello_triangle PRIVATE Nevarea::Nevarea glfw)
set_target_properties(hello_triangle PROPERTIES
  CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)

if(WIN32 AND NEVAREA_BUILD_DYNAMIC)
  add_custom_command(TARGET hello_triangle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Nevarea> $<TARGET_FILE_DIR:hello_triangle>)
endif()
