cmake_minimum_required(VERSION 2.8.7)

project(iio_genxml  C)
project(iio_info    C)
project(iio_attr    C)
project(iio_readdev C)
project(iio_reg     C)

if(APPLE)
	# Add relative rpath to iio library (which is in the same framework)
	set(CMAKE_INSTALL_RPATH "@executable_path/../..")
	set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
endif()

if (MSVC)
	include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../deps/wingetopt/src)
	set(GETOPT_C_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../deps/wingetopt/src/getopt.c)
	set_source_files_properties(${GETOPT_C_FILE} PROPERTIES COMPILE_FLAGS -D_CRT_SECURE_NO_WARNINGS=1)
endif (MSVC)

if (WIN32)
	set(LIBIIO_ORIGINAL_FILENAME )
	set(LIBIIO_RC ${CMAKE_CURRENT_BINARY_DIR}/properties.rc)
	configure_file(../properties.rc.cmakein ${LIBIIO_RC} @ONLY)
endif()

if (CMAKE_COMPILER_IS_GNUCC)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DIIO_CHECK_RET")
endif()
add_library(iio_tests_helper STATIC iio_common.c gen_code.c)
target_link_libraries(iio_tests_helper iio)

add_executable(iio_genxml  iio_genxml.c   ${GETOPT_C_FILE} ${LIBIIO_RC})
add_executable(iio_info    iio_info.c     ${GETOPT_C_FILE} ${LIBIIO_RC})
add_executable(iio_attr    iio_attr.c     ${GETOPT_C_FILE} ${LIBIIO_RC})
add_executable(iio_readdev iio_readdev.c  ${GETOPT_C_FILE} ${LIBIIO_RC})
add_executable(iio_reg     iio_reg.c      ${GETOPT_C_FILE} ${LIBIIO_RC})
add_executable(iio_writedev iio_writedev.c  ${GETOPT_C_FILE} ${LIBIIO_RC})

target_link_libraries(iio_genxml  iio iio_tests_helper)
target_link_libraries(iio_info    iio iio_tests_helper)
target_link_libraries(iio_attr    iio iio_tests_helper)
target_link_libraries(iio_readdev iio iio_tests_helper)
target_link_libraries(iio_reg     iio iio_tests_helper)
target_link_libraries(iio_writedev iio iio_tests_helper)

set(IIO_TESTS_TARGETS iio_genxml iio_info iio_attr iio_readdev iio_reg iio_writedev)

if(PTHREAD_LIBRARIES OR ANDROID)
	project(iio_adi_xflow_check C)
	project(iio_stresstest C)
	add_executable(iio_adi_xflow_check iio_adi_xflow_check.c)
	add_executable(iio_stresstest iio_stresstest.c)
	target_link_libraries(iio_adi_xflow_check iio iio_tests_helper ${PTHREAD_LIBRARIES})
	target_link_libraries(iio_stresstest iio iio_tests_helper ${PTHREAD_LIBRARIES})
	set(IIO_TESTS_TARGETS ${IIO_TESTS_TARGETS} iio_adi_xflow_check iio_stresstest)

	target_link_libraries(iio_readdev  ${PTHREAD_LIBRARIES})
	target_link_libraries(iio_writedev ${PTHREAD_LIBRARIES})
endif()

set_target_properties(${IIO_TESTS_TARGETS} PROPERTIES
	C_STANDARD 99
	C_STANDARD_REQUIRED ON
	C_EXTENSIONS OFF
)

if(NOT SKIP_INSTALL_ALL)
	if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
		install(TARGETS ${IIO_TESTS_TARGETS} RUNTIME DESTINATION ${OSX_INSTALL_FRAMEWORKSDIR}/iio.framework/Tools)
	else()
		install(TARGETS ${IIO_TESTS_TARGETS} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	endif()
endif()

# Make the test targets available to the main CMakeLists.txt
set(IIO_TESTS_TARGETS ${IIO_TESTS_TARGETS} PARENT_SCOPE)
