cmake_minimum_required(VERSION 3.10)
project(tinyiiod C)

add_library(tinyiiod STATIC
	tinyiiod.c
	${CMAKE_SOURCE_DIR}/iiod/responder.c
	${CMAKE_SOURCE_DIR}/iiod/rw.c
)
target_include_directories(tinyiiod PRIVATE
	${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(tinyiiod PROPERTIES
	VERSION ${LIBIIO_VERSION_MAJOR}.${LIBIIO_VERSION_MINOR}
	SOVERSION ${LIBIIO_VERSION_MAJOR}
	PUBLIC_HEADER tinyiiod.h
	C_STANDARD 99
	C_STANDARD_REQUIRED ON
	C_EXTENSIONS OFF
)
target_link_libraries(tinyiiod LINK_PRIVATE iio iiod-responder)
