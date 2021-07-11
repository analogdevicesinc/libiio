cmake_minimum_required(VERSION 2.8.7)
project(iiod C)

message(STATUS "Building iiod" )

option(WITH_SYSTEMD "Enable installation of systemd service file for iiod" OFF)
option(WITH_SYSVINIT "Enable installation of SysVinit script for iiod" OFF)
option(WITH_UPSTART "Enable installation of upstart config file for iiod" OFF)

if ((WITH_SYSTEMD AND WITH_SYSVINIT) OR (WITH_SYSTEMD AND WITH_UPSTART) OR
		(WITH_SYSVINIT AND WITH_UPSTART))
		message(FATAL_ERROR "Multiple init systems enabled: only enable one")
endif()
if (NOT WITH_SYSTEMD AND NOT WITH_SYSVINIT AND NOT WITH_UPSTART)
	message(STATUS "Init system not configured: use WITH_SYSTEMD, WITH_SYSVINIT or WITH_UPSTART")
endif()

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

	message(STATUS "Looking for libaio: Found")

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
	install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/init/iiod_create_ini DESTINATION ${CMAKE_INSTALL_SBINDIR})
	install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/init/iiod_setup_gadgetfs DESTINATION ${CMAKE_INSTALL_SBINDIR})
endif()

if (WITH_SYSTEMD)
	# Borrowed a portion of this from https://github.com/intel/hdcp/blob/master/config/systemdservice.cmake
	# Copyright (c) 2018, Intel Corporation
	pkg_check_modules(SYSTEMD "systemd")
	if (SYSTEMD_FOUND AND "${SYSTEMD_SERVICES_INSTALL_DIR}" STREQUAL "")
		execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE}
			--variable=systemdsystemunitdir systemd
			OUTPUT_VARIABLE SYSTEMD_SERVICES_INSTALL_DIR)
		string(REGEX REPLACE "[ \t\n]+" "" SYSTEMD_SERVICES_INSTALL_DIR
			"${SYSTEMD_SERVICES_INSTALL_DIR}")
	else()
		set(SYSTEMD_SERVICES_INSTALL_DIR /lib/systemd/system CACHE PATH "default install path for systemd unit files")
	endif()
	message(STATUS "Configured for the systemd init system, installing into " ${SYSTEMD_SERVICES_INSTALL_DIR})
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/init/iiod.service.cmakein ${PROJECT_BINARY_DIR}/init/iiod.service)
	install(FILES ${PROJECT_BINARY_DIR}/init/iiod.service DESTINATION ${SYSTEMD_SERVICES_INSTALL_DIR})
endif()

if (WITH_SYSVINIT)
	set(SYSVINIT_INSTALL_DIR /etc/init.d CACHE PATH "default install path for SysVinit scripts")
	message(STATUS "Configured for the SysVinit system, installing into " ${SYSVINIT_INSTALL_DIR})
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/init/iiod.init.cmakein ${PROJECT_BINARY_DIR}/init/iiod)
	install(FILES ${PROJECT_BINARY_DIR}/init/iiod
	        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
	        DESTINATION ${SYSVINIT_INSTALL_DIR})
endif()

if (WITH_UPSTART)
	set(UPSTART_CONF_INSTALL_DIR /etc/init CACHE PATH "default install path for upstart conf files")
	message(STATUS "Configured for the Upstart init system, installing into "  ${UPSTART_CONF_INSTALL_DIR})
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/init/iiod.conf.cmakein ${PROJECT_BINARY_DIR}/init/iiod.conf)
	install(FILES ${PROJECT_BINARY_DIR}/init/iiod.conf DESTINATION ${UPSTART_CONF_INSTALL_DIR})
endif()
