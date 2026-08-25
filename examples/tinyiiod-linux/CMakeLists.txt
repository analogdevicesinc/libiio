cmake_minimum_required(VERSION 3.13)

project(tinyiiod-server C)

add_executable(tinyiiod-server tinyiiod-server.c)

target_link_libraries(tinyiiod-server PRIVATE iio tinyiiod iio_common_config m)

if (UNIX AND NOT APPLE)
	# Link pthread for multi-threaded client handling
	find_package(Threads REQUIRED)
	target_link_libraries(tinyiiod-server PRIVATE Threads::Threads)
endif()
