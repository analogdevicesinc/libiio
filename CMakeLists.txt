cmake_minimum_required(VERSION 2.8.7)
project(libiio C)

set(LIBIIO_VERSION_MAJOR 0)
set(LIBIIO_VERSION_MINOR 5)
set(VERSION "${LIBIIO_VERSION_MAJOR}.${LIBIIO_VERSION_MINOR}")

# Set the default install path to /usr
if (NOT WIN32 AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "default install path" FORCE)
endif()

set(INSTALL_BIN_DIR "${CMAKE_INSTALL_PREFIX}/bin"
	CACHE PATH "Installation directory for binaries")
set(INSTALL_LIB_DIR "${CMAKE_INSTALL_PREFIX}/lib"
	CACHE PATH "Installation directory for libraries")
set(INSTALL_INC_DIR "${CMAKE_INSTALL_PREFIX}/include"
	CACHE PATH "Installation directory for headers")
set(INSTALL_PKGCONFIG_DIR "${INSTALL_LIB_DIR}/pkgconfig"
	CACHE PATH "Installation directory for pkgconfig (.pc) files")
set(INSTALL_DATA_DIR "${CMAKE_INSTALL_PREFIX}/share"
	CACHE PATH "Installation directory for data")

mark_as_advanced(INSTALL_BIN_DIR)
mark_as_advanced(INSTALL_LIB_DIR)
mark_as_advanced(INSTALL_INC_DIR)
mark_as_advanced(INSTALL_PKGCONFIG_DIR)
mark_as_advanced(INSTALL_DATA_DIR)

set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries")

option(WITH_NETWORK_BACKEND "Enable the network backend" ON)

if (NOT MSVC)
	option(WITH_TESTS "Build the test programs" ON)
else()
	# Avoid annoying warnings from Visual Studio
	add_definitions(-D_CRT_SECURE_NO_WARNINGS=1)
endif()

if (NOT WIN32)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
endif()

IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	option(WITH_IIOD "Build the IIO Daemon" ON)
	option(WITH_LOCAL_BACKEND "Enable the local backend" ON)

	if (WITH_IIOD AND NOT WITH_LOCAL_BACKEND)
		message(SEND_ERROR "IIOD can only be enabled if the local backend is enabled")
	endif()
endif()

option(ENABLE_IPV6 "Define if you want to enable IPv6 support" ON)

include(CheckSymbolExists)
check_symbol_exists(in6addr_any "netinet/in.h" HAVE_IPV6)

if (ENABLE_IPV6)
	if (NOT HAVE_IPV6)
		message(WARNING "IPv6 is not available in your system.")
	else()
		add_definitions(-DHAVE_IPV6=1)
	endif()
endif()

find_library(AVAHI_CLIENT_LIBRARIES avahi-client)
find_library(AVAHI_COMMON_LIBRARIES avahi-common)
find_library(PTHREAD_LIBRARIES pthread)
include(FindLibXml2)

set(LIBIIO_CFILES channel.c device.c context.c buffer.c utilities.c)
set(LIBIIO_HEADERS iio.h)

add_definitions(-D_POSIX_C_SOURCE=200809L -DLIBIIO_EXPORTS=1)

# Get the GIT hash of the latest commit
include(FindGit OPTIONAL)
if (GIT_FOUND)
	execute_process(
		COMMAND git rev-parse --show-toplevel
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		OUTPUT_VARIABLE LIBIIO_GIT_REPO
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)

	if (${LIBIIO_GIT_REPO} MATCHES ${CMAKE_SOURCE_DIR})
		execute_process(
			COMMAND git rev-parse --short HEAD
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			OUTPUT_VARIABLE LIBIIO_VERSION_GIT
			OUTPUT_STRIP_TRAILING_WHITESPACE
		)
	endif()
endif()

if (NOT LIBIIO_VERSION_GIT)
	set(LIBIIO_VERSION_GIT v${VERSION})
endif()

add_definitions(-DLIBIIO_VERSION_MAJOR=${LIBIIO_VERSION_MAJOR})
add_definitions(-DLIBIIO_VERSION_MINOR=${LIBIIO_VERSION_MINOR})
add_definitions(-DLIBIIO_VERSION_GIT="${LIBIIO_VERSION_GIT}")

if(WITH_LOCAL_BACKEND)
	add_definitions(-DLOCAL_BACKEND=1)
	set(LIBIIO_CFILES ${LIBIIO_CFILES} local.c)
endif()

include_directories(${CMAKE_SOURCE_DIR})

if(WITH_NETWORK_BACKEND)
	if (NOT LIBXML2_FOUND)
		message(SEND_ERROR "The network backend requires libxml2 to be installed")
	endif()

	if (WIN32)
		set(LIBS_TO_LINK ${LIBS_TO_LINK} wsock32 ws2_32)
	endif()

	if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
		include(CheckCSourceCompiles)
		set(CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE=1")
		check_c_source_compiles("#include <fcntl.h>\nint main(void) { return O_TMPFILE; }" HAS_O_TMPFILE)

		if (HAS_O_TMPFILE)
			add_definitions(-D_GNU_SOURCE=1 -DWITH_NETWORK_GET_BUFFER=1)
		endif()
	endif()

	add_definitions(-DNETWORK_BACKEND=1 ${LIBXML2_DEFINITIONS})
	include_directories(${LIBXML2_INCLUDE_DIR})
	set(LIBIIO_CFILES ${LIBIIO_CFILES} network.c xml.c)
	set(LIBS_TO_LINK ${LIBS_TO_LINK} ${LIBXML2_LIBRARIES})

	if(AVAHI_CLIENT_LIBRARIES AND AVAHI_COMMON_LIBRARIES)
		add_definitions(-DHAVE_AVAHI=1)
		set(AVAHI_LIBRARIES ${AVAHI_CLIENT_LIBRARIES} ${AVAHI_COMMON_LIBRARIES})
		set(LIBS_TO_LINK ${LIBS_TO_LINK} ${AVAHI_LIBRARIES})
	endif()

	if(PTHREAD_LIBRARIES)
		add_definitions(-DHAVE_PTHREAD=1)
		set(LIBS_TO_LINK ${LIBS_TO_LINK} ${PTHREAD_LIBRARIES})
	else()
		add_definitions(-DHAVE_PTHREAD=0)
	endif()
endif()

set(LIBIIO_PC ${CMAKE_CURRENT_BINARY_DIR}/libiio.pc)
configure_file(libiio.pc.cmakein ${LIBIIO_PC} @ONLY)
install(FILES ${LIBIIO_PC} DESTINATION "${INSTALL_PKGCONFIG_DIR}")

#set(SETUP_PY ${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/setup.py)
#configure_file(python/setup.py.in ${SETUP_PY} @ONLY)

add_library(libiio ${LIBIIO_CFILES} ${LIBIIO_HEADERS})
set_target_properties(libiio PROPERTIES VERSION ${VERSION} SOVERSION ${LIBIIO_VERSION_MAJOR})
target_link_libraries(libiio LINK_PRIVATE ${LIBS_TO_LINK})

if (NOT SKIP_INSTALL_HEADERS AND NOT SKIP_INSTALL_ALL)
	install(FILES ${LIBIIO_HEADERS} DESTINATION "${INSTALL_INC_DIR}")
endif()

if(NOT SKIP_INSTALL_LIBRARIES AND NOT SKIP_INSTALL_ALL)
	install(TARGETS libiio
		ARCHIVE DESTINATION "${INSTALL_LIB_DIR}"
		LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
		RUNTIME DESTINATION "${INSTALL_BIN_DIR}")
endif()

find_package(Doxygen)
if(DOXYGEN_FOUND)
	option(WITH_DOC "Generate documentation with Doxygen" ON)

	if (WITH_DOC)
		configure_file(${CMAKE_SOURCE_DIR}/Doxyfile.in ${CMAKE_BINARY_DIR}/Doxyfile @ONLY)
		set(HTML_DEST_DIR ${CMAKE_BINARY_DIR}/html)
		file(COPY ${CMAKE_SOURCE_DIR}/doc DESTINATION ${HTML_DEST_DIR})

		add_custom_command(TARGET libiio POST_BUILD
			COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile
			WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
			COMMENT "Generating API documentation with Doxygen" VERBATIM
			)

		install(DIRECTORY ${HTML_DEST_DIR}
			DESTINATION ${INSTALL_DATA_DIR}/doc/libiio${LIBIIO_VERSION_MAJOR}-doc)
	endif()
else()
	message(STATUS "Doxygen not found, API documentation won't be generated")
endif()

if(WITH_IIOD)
	add_subdirectory(iiod)
endif()

if(WITH_TESTS)
	add_subdirectory(tests)
endif()

add_subdirectory(bindings)
