add_executable(TestBufferAD9361 test_ad9364.c)
target_link_libraries(TestBufferAD9361 LINK_PRIVATE iio)
add_test(
  NAME TestBufferAD9361
  COMMAND TestBufferAD9361
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
