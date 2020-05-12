cmake_minimum_required(VERSION 2.8.7)
project(iiod C)

include(FindBISON)
include(FindFLEX)

flex_target(lexer
	${CMAKE_CURRENT_SOURCE_DIR}/lexer.l ${CMAKE_CURRENT_BINARY_DIR}/lexer.c)
bison_target(parser
	${CMAKE_CURRENT_SOURCE_DIR}/parser.y ${CMAKE_CURRENT_BINARY_DIR}/parser.c)
add_flex_bison_dependency(lexer parser)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

if (CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_COMPILER_IS_GNUCC)
	# flex sometimes generates code which generate sign comparison errors
	set_source_files_properties(${FLEX_lexer_OUTPUTS} PROPERTIES COMPILE_FLAGS "-Wno-sign-compare")
	set_source_files_properties(${BISON_parser_OUTPUTS} PROPERTIES COMPILE_FLAGS "-Wno-sign-compare")
endif ()

set(IIOD_CFILES iiod.c ops.c thread-pool.c ${BISON_parser_OUTPUTS} ${FLEX_lexer_OUTPUTS})

find_library(LIBAIO_LIBRARIES aio)
find_path(LIBAIO_INCLUDE_DIR libaio.h)

if (LIBAIO_LIBRARIES AND LIBAIO_INCLUDE_DIR)
	option(ENABLE_AIO "Build IIOD with async. I/O support" ON)
endif ()

include(CheckTypeSize)
set(CMAKE_EXTRA_INCLUDE_FILES linux/usb/functionfs.h)
check_type_size("struct usb_functionfs_descs_head_v2" FUNCTIONFS_V2)
set(CMAKE_EXTRA_INCLUDE_FILES)

if (HAVE_FUNCTIONFS_V2)
	OPTION(WITH_IIOD_USBD "Add support for USB through FunctionFS within IIOD" ${ENABLE_AIO})

	if (WITH_IIOD_USBD)
		if (NOT ENABLE_AIO)
			message(SEND_ERROR "USB support in IIOD requires async. I/O support")
		endif (NOT ENABLE_AIO)

		set(IIOD_CFILES ${IIOD_CFILES} usbd.c)
	endif (WITH_IIOD_USBD)
endif (HAVE_FUNCTIONFS_V2)

add_executable(iiod ${IIOD_CFILES})
set_target_properties(iiod PROPERTIES
	C_STANDARD 99
	C_STANDARD_REQUIRED ON
	C_EXTENSIONS OFF
)
target_link_libraries(iiod iio ${PTHREAD_LIBRARIES} ${AVAHI_LIBRARIES})

if (ENABLE_AIO)
	add_definitions(-DWITH_AIO=1)
	include_directories(${LIBAIO_INCLUDE_DIR})
	target_link_libraries(iiod ${LIBAIO_LIBRARIES})
endif ()

add_definitions(-D_GNU_SOURCE=1)

if(NOT SKIP_INSTALL_ALL)
	install(TARGETS iiod RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})
endif()

if (WITH_SYSTEMD)
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/init/iiod.service.cmakein ${PROJECT_BINARY_DIR}/init/iiod.service)
	install(FILES ${PROJECT_BINARY_DIR}/init/iiod.service DESTINATION ${SYSTEMD_UNIT_INSTALL_DIR})
endif()

if (WITH_SYSVINIT)
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/init/iiod.init.cmakein ${PROJECT_BINARY_DIR}/init/iiod)
	install(FILES ${PROJECT_BINARY_DIR}/init/iiod
	        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
	        DESTINATION ${SYSVINIT_INSTALL_DIR})
endif()

if (WITH_UPSTART)
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/init/iiod.conf.cmakein ${PROJECT_BINARY_DIR}/init/iiod.conf)
	install(FILES ${PROJECT_BINARY_DIR}/init/iiod.conf DESTINATION ${UPSTART_CONF_INSTALL_DIR})
endif()
