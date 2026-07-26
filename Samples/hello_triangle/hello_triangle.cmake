if(WIN32 AND NEVAREA_BUILD_DYNAMIC)
  add_custom_command(TARGET hello_triangle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Nevarea> $<TARGET_FILE_DIR:hello_triangle>)
endif()
