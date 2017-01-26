cmake_minimum_required(VERSION 2.8.7)

project(iio_genxml  C)
project(iio_info    C)
project(iio_readdev C)
project(iio_reg     C)

if (MSVC)
	include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../deps/wingetopt/src)
	set(GETOPT_C_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../deps/wingetopt/src/getopt.c)
endif (MSVC)

add_executable(iio_genxml  iio_genxml.c   ${GETOPT_C_FILE})
add_executable(iio_info    iio_info.c     ${GETOPT_C_FILE})
add_executable(iio_readdev iio_readdev.c  ${GETOPT_C_FILE})
add_executable(iio_reg     iio_reg.c      ${GETOPT_C_FILE})

target_link_libraries(iio_genxml  iio)
target_link_libraries(iio_info    iio)
target_link_libraries(iio_readdev iio)
target_link_libraries(iio_reg     iio)

set(IIO_TESTS_TARGETS iio_genxml iio_info iio_readdev iio_reg)

if(PTHREAD_LIBRARIES)
	project(iio_adi_xflow_check C)
	add_executable(iio_adi_xflow_check iio_adi_xflow_check.c)
	target_link_libraries(iio_adi_xflow_check iio ${PTHREAD_LIBRARIES})
	set(IIO_TESTS_TARGETS ${IIO_TESTS_TARGETS} iio_adi_xflow_check)

	target_link_libraries(iio_readdev ${PTHREAD_LIBRARIES})
endif()

set_target_properties(${IIO_TESTS_TARGETS} PROPERTIES
	C_STANDARD 99
	C_STANDARD_REQUIRED ON
	C_EXTENSIONS OFF
)

if(NOT SKIP_INSTALL_ALL)
	install(TARGETS ${IIO_TESTS_TARGETS} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# Make the test targets available to the main CMakeLists.txt
set(IIO_TESTS_TARGETS ${IIO_TESTS_TARGETS} PARENT_SCOPE)
