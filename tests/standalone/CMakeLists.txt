add_executable(TestVersion test_version.c)
target_link_libraries(TestVersion LINK_PRIVATE iio)
add_test(
  NAME TestVersion
  COMMAND TestVersion
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
