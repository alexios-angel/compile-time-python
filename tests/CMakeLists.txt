# the suite is static_asserts: compiling a test IS running it
add_custom_target(ctpy-test ALL)

file(GLOB test-sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# tests with a main() run; the rest are static_assert suites where
# compiling is the test
set(runtime-tests "")

foreach(source ${test-sources})
  get_filename_component(test "${source}" NAME_WE)
  if(test IN_LIST runtime-tests)
    add_executable(ctpy-test-${test} "${source}")
  else()
    add_library(ctpy-test-${test} OBJECT EXCLUDE_FROM_ALL "${source}")
  endif()
  target_link_libraries(ctpy-test-${test} PRIVATE ctpy::ctpy)
  if(CTPY_PCH AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
    target_precompile_headers(ctpy-test-${test} PRIVATE
      "${PROJECT_SOURCE_DIR}/include/ctpy.hpp")
  endif()
  add_dependencies(ctpy-test ctpy-test-${test})
  if(test IN_LIST runtime-tests)
    add_test(NAME run-${test} COMMAND ctpy-test-${test})
  else()
    add_test(NAME compile-${test}
      COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target ctpy-test-${test})
  endif()
endforeach()
