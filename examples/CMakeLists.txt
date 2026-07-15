file(GLOB example-sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

foreach(source ${example-sources})
  get_filename_component(example "${source}" NAME_WE)
  add_executable(ctpy-example-${example} "${source}")
  target_link_libraries(ctpy-example-${example} PRIVATE ctpy::ctpy)
  if(CTPY_PCH AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
    target_precompile_headers(ctpy-example-${example} PRIVATE
      "${PROJECT_SOURCE_DIR}/include/ctpy.hpp")
  endif()
  add_test(NAME example-${example} COMMAND ctpy-example-${example})
endforeach()
