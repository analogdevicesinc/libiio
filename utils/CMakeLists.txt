cmake_minimum_required(VERSION 3.10)

set(IIO_TESTS_TARGETS iio_genxml iio_info iio_attr iio_rwdev iio_reg iio_event)
if (PTHREAD_LIBRARIES OR ANDROID)
	LIST(APPEND IIO_TESTS_TARGETS iio_stresstest)
endif()

if(APPLE)
	# Add relative rpath to iio library (which is in the same framework)
	set(CMAKE_INSTALL_RPATH "@executable_path/../..")
	set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
endif()

if (WIN32)
	set(LIBIIO_ORIGINAL_FILENAME )
	set(LIBIIO_RC ${CMAKE_CURRENT_BINARY_DIR}/properties.rc)
	configure_file(../properties.rc.cmakein ${LIBIIO_RC} @ONLY)
endif()

add_library(iio_tests_helper STATIC iio_common.c gen_code.c)
target_link_libraries(iio_tests_helper LINK_PRIVATE iio)
if (MSVC)
	target_sources(iio_tests_helper PRIVATE ../deps/wingetopt/src/getopt.c)
	target_include_directories(iio_tests_helper PUBLIC ../deps/wingetopt/src)
	target_compile_definitions(iio_tests_helper PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_link_options(iio_tests_helper PUBLIC /DEBUG)
endif()
set_target_properties(iio_tests_helper PROPERTIES
	C_STANDARD 99
	C_STANDARD_REQUIRED ON
	C_EXTENSIONS OFF
)

foreach (test ${IIO_TESTS_TARGETS})
	project(${test} LANGUAGES C)
	add_executable(${test} ${test}.c ${GETOPT_C_FILE} ${LIBIIO_RC})
	set_target_properties(${test} PROPERTIES
		C_STANDARD 99
		C_STANDARD_REQUIRED ON
		C_EXTENSIONS OFF
	)
	if (CMAKE_COMPILER_IS_GNUCC)
		target_compile_definitions(${test} PRIVATE IIO_CHECK_RET)
	endif()
	target_link_libraries(${test} LINK_PRIVATE iio iio_tests_helper)
	if (MSVC)
		target_link_options(${test} PUBLIC /DEBUG)
	endif()
endforeach()

if(PTHREAD_LIBRARIES)
	set(IIO_TESTS_NEED_PTHREAD iio_rwdev iio_stresstest)

	foreach(test ${IIO_TESTS_NEED_PTHREAD})
		target_link_libraries(${test} LINK_PRIVATE ${PTHREAD_LIBRARIES})
	endforeach()
endif()

# Make the test targets available to the main CMakeLists.txt
set(IIO_TESTS_TARGETS ${IIO_TESTS_TARGETS} PARENT_SCOPE)
