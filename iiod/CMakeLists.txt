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

include(CheckSymbolExists)
set(CMAKE_REQUIRED_LIBRARIES ${PTHREAD_LIBRARIES})
set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
check_symbol_exists(pthread_setname_np "pthread.h" HAS_PTHREAD_SETNAME_NP)
set(CMAKE_REQUIRED_LIBRARIES)
set(CMAKE_REQUIRED_DEFINITIONS)

if (CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_COMPILER_IS_GNUCC)
	# flex sometimes generates code which generate sign comparison errors
	set_source_files_properties(${FLEX_lexer_OUTPUTS} PROPERTIES COMPILE_FLAGS "-Wno-sign-compare")
	set_source_files_properties(${BISON_parser_OUTPUTS} PROPERTIES COMPILE_FLAGS "-Wno-sign-compare")
endif ()

set(IIOD_CFILES iiod.c ops.c thread-pool.c ${BISON_parser_OUTPUTS} ${FLEX_lexer_OUTPUTS})

option(WITH_AIO "Build IIOD with async. I/O support" ON)
if (WITH_AIO)
	find_library(LIBAIO_LIBRARIES aio)
	find_path(LIBAIO_INCLUDE_DIR libaio.h)

	if (NOT LIBAIO_LIBRARIES OR NOT LIBAIO_INCLUDE_DIR)
		message(SEND_ERROR "Unable to find libaio dependency.\n"
			"If you want to disable async. I/O support, set WITH_AIO=OFF.")
	endif ()

	option(WITH_IIOD_USBD "Add support for USB through FunctionFS within IIOD" ON)
	if (WITH_IIOD_USBD)
		include(CheckTypeSize)
		set(CMAKE_EXTRA_INCLUDE_FILES linux/usb/functionfs.h)
		check_type_size("struct usb_functionfs_descs_head_v2" FUNCTIONFS_V2)
		set(CMAKE_EXTRA_INCLUDE_FILES)

		if (NOT HAVE_FUNCTIONFS_V2)
			message(SEND_ERROR "Linux kernel headers are too old.\n"
				"If you want to disable USB support, set WITH_IIOD_USBD=OFF.")
		endif()

		set(IIOD_CFILES ${IIOD_CFILES} usbd.c)
	endif()
endif()

option(WITH_IIOD_SERIAL "Add serial (UART) support" ON)
if (WITH_IIOD_SERIAL)
	set(IIOD_CFILES ${IIOD_CFILES} serial.c)
endif()

add_executable(iiod ${IIOD_CFILES})
set_target_properties(iiod PROPERTIES
	C_STANDARD 99
	C_STANDARD_REQUIRED ON
	C_EXTENSIONS OFF
)
target_link_libraries(iiod iio ${PTHREAD_LIBRARIES} ${AVAHI_LIBRARIES})

if (WITH_AIO)
	include_directories(${LIBAIO_INCLUDE_DIR})
	target_link_libraries(iiod ${LIBAIO_LIBRARIES})
endif ()

if (WITH_ZSTD)
	include_directories(${LIBZSTD_INCLUDE_DIR})
	target_link_libraries(iiod ${LIBZSTD_LIBRARIES})
endif()

add_definitions(-D_GNU_SOURCE=1)

if (HAVE_AVAHI)
	target_sources(iiod PRIVATE dns-sd.c)
endif()

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

list(APPEND IIOD_FEATURES_${WITH_IIOD_SERIAL} iiod-serial)
list(APPEND IIOD_FEATURES_${WITH_AIO} iiod-aio)
list(APPEND IIOD_FEATURES_${WITH_IIOD_USBD} iiod-usb)
list(APPEND IIOD_FEATURES_${WITH_SYSTEMD} iiod-systemd)
list(APPEND IIOD_FEATURES_${WITH_SYSVINIT} iiod-sysvinit)
list(APPEND IIOD_FEATURES_${WITH_UPSTART} iiod-upstart)

set(IIOD_FEATURES_ON "${IIOD_FEATURES_ON}" PARENT_SCOPE)
set(IIOD_FEATURES_OFF "${IIOD_FEATURES_OFF}" PARENT_SCOPE)
