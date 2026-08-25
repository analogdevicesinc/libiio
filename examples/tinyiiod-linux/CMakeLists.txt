cmake_minimum_required(VERSION 3.13)

project(tinyiiod-server C)

add_executable(tinyiiod-server tinyiiod-server.c)

target_link_libraries(tinyiiod-server PRIVATE iio tinyiiod iio_common_config m)

endif()
