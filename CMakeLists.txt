cmake_minimum_required(VERSION 2.8.7)
project(libiio C)

set(LIBIIO_VERSION_MAJOR 0)
set(LIBIIO_VERSION_MINOR 6)
set(VERSION "${LIBIIO_VERSION_MAJOR}.${LIBIIO_VERSION_MINOR}")

# Set the default install path to /usr
if (NOT WIN32 AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "default install path" FORCE)
endif()

set(CMAKE_INSTALL_DOCDIR "" CACHE PATH "documentation root (DATAROOTDIR/doc/${PROJECT_NAME}${LIBIIO_VERSION_MAJOR}-doc)")
include(GNUInstallDirs)
set(CMAKE_INSTALL_DOCDIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/${PROJECT_NAME}${LIBIIO_VERSION_MAJOR}-doc")

set(INSTALL_PKGCONFIG_DIR "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
	CACHE PATH "Installation directory for pkgconfig (.pc) files")
mark_as_advanced(INSTALL_PKGCONFIG_DIR)

if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
		"Choose the type of build, options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release RelWithDebInfo MinSizeRel."
		FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS None Debug Release RelWithDebInfo MinSizeRel)
endif()

set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries")

option(WITH_NETWORK_BACKEND "Enable the network backend" ON)
option(WITH_TESTS "Build the test programs" ON)

if (MSVC)
	# Avoid annoying warnings from Visual Studio
	add_definitions(-D_CRT_SECURE_NO_WARNINGS=1)
endif()

if (NOT WIN32)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
endif()

if (CMAKE_COMPILER_IS_GNUCC)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare")

	include(CheckCCompilerFlag)
	check_c_compiler_flag(-Wpedantic HAS_WPEDANTIC)
	if (HAS_WPEDANTIC)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wpedantic")
	endif()
endif()

IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	option(WITH_IIOD "Build the IIO Daemon" ON)
	option(WITH_LOCAL_BACKEND "Enable the local backend" ON)

	if (WITH_IIOD AND NOT WITH_LOCAL_BACKEND)
		message(SEND_ERROR "IIOD can only be enabled if the local backend is enabled")
	endif()
	if (WITH_IIOD)
		set(NEED_THREADS 1)
	endif()

	set(CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE=1")
	add_definitions(-D_GNU_SOURCE=1)
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

set(LIBIIO_CFILES channel.c device.c context.c buffer.c utilities.c scan.c)
set(LIBIIO_HEADERS iio.h)

add_definitions(-D_POSIX_C_SOURCE=200809L -D__XSI_VISIBLE=500 -DLIBIIO_EXPORTS=1)

# Get the GIT hash of the latest commit
include(FindGit OPTIONAL)
if (GIT_FOUND)
	execute_process(
		COMMAND ${GIT_EXECUTABLE} rev-parse --show-toplevel
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		OUTPUT_VARIABLE LIBIIO_GIT_REPO
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)

	if (${LIBIIO_GIT_REPO} MATCHES ${CMAKE_CURRENT_SOURCE_DIR})
		execute_process(
			COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
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

find_library(LIBUSB_LIBRARIES usb-1.0)
find_path(LIBUSB_INCLUDE_DIR libusb-1.0/libusb.h)
if (LIBUSB_LIBRARIES AND LIBUSB_INCLUDE_DIR)
	option(WITH_USB_BACKEND "Enable the libusb backend" ON)

	if(WITH_USB_BACKEND)
		add_definitions(-DUSB_BACKEND=1)
		set(LIBIIO_CFILES ${LIBIIO_CFILES} usb.c)
		set(LIBS_TO_LINK ${LIBS_TO_LINK} ${LIBUSB_LIBRARIES})
		set(IIOD_CLIENT 1)
		set(NEED_LIBXML2 1)

		include_directories(${LIBUSB_INCLUDE_DIR})
	endif()
endif()

find_library(LIBSERIALPORT_LIBRARIES serialport)
find_path(LIBSERIALPORT_INCLUDE_DIR libserialport.h)
if (LIBSERIALPORT_LIBRARIES AND LIBSERIALPORT_INCLUDE_DIR)
	option(WITH_SERIAL_BACKEND "Enable the serial backend" ON)

	if (WITH_SERIAL_BACKEND)
		add_definitions(-DSERIAL_BACKEND=1)
		set(LIBIIO_CFILES ${LIBIIO_CFILES} serial.c)
		set(LIBS_TO_LINK ${LIBS_TO_LINK} ${LIBSERIALPORT_LIBRARIES})

		set(NEED_THREADS 1)
		set(IIOD_CLIENT 1)
		set(NEED_LIBXML2 1)

		include_directories(${LIBSERIALPORT_INCLUDE_DIR})
	endif()
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

if(WITH_NETWORK_BACKEND)
	if (WIN32)
		set(LIBS_TO_LINK ${LIBS_TO_LINK} wsock32 ws2_32)
	endif()

	if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
		include(CheckCSourceCompiles)
		check_c_source_compiles("#include <fcntl.h>\nint main(void) { return O_TMPFILE; }" HAS_O_TMPFILE)

		if (HAS_O_TMPFILE)
			option(WITH_NETWORK_GET_BUFFER "Enable experimental zero-copy transfers" OFF)

			if (WITH_NETWORK_GET_BUFFER)
				add_definitions(-DWITH_NETWORK_GET_BUFFER=1)
			endif(WITH_NETWORK_GET_BUFFER)
		endif(HAS_O_TMPFILE)

		check_c_source_compiles("#include <sys/eventfd.h>\nint main(void) { return eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK); }" HAS_EVENTFD)

		if (HAS_EVENTFD)
			add_definitions(-DWITH_NETWORK_EVENTFD=1)
		endif(HAS_EVENTFD)
	endif()

	if(NOT WIN32)
		include(CheckCSourceCompiles)
		check_c_source_compiles("#include <unistd.h>\n#include <fcntl.h>\nint main(void) { int fd[2]; return pipe2(fd, O_CLOEXEC | O_NONBLOCK); }" HAS_PIPE2)

		if (HAS_PIPE2)
			add_definitions(-DHAS_PIPE2=1)
		endif(HAS_PIPE2)
	endif()

	add_definitions(-DNETWORK_BACKEND=1)
	set(LIBIIO_CFILES ${LIBIIO_CFILES} network.c)

	find_library(AVAHI_CLIENT_LIBRARIES avahi-client)
	find_library(AVAHI_COMMON_LIBRARIES avahi-common)
	if(AVAHI_CLIENT_LIBRARIES AND AVAHI_COMMON_LIBRARIES)
		add_definitions(-DHAVE_AVAHI=1)
		set(AVAHI_LIBRARIES ${AVAHI_CLIENT_LIBRARIES} ${AVAHI_COMMON_LIBRARIES})
		set(LIBS_TO_LINK ${LIBS_TO_LINK} ${AVAHI_LIBRARIES})
	endif()

	set(NEED_THREADS 1)
	set(IIOD_CLIENT 1)
	set(NEED_LIBXML2 1)
endif()

# Since libxml2-2.9.2, libxml2 provides its own LibXml2-config.cmake, with all
# variables correctly set.
# So, try first to find the CMake module provided by libxml2 package, then fallback
# on the CMake's FindLibXml2.cmake module (which can lack some definition, especially
# in static build case).
find_package(LibXml2 QUIET NO_MODULE)
if(DEFINED LIBXML2_VERSION_STRING)
	set(LIBXML2_FOUND ON)
	set(LIBXML2_INCLUDE_DIR ${LIBXML2_INCLUDE_DIRS})
else()
	include(FindLibXml2)
endif()

if (LIBXML2_FOUND)
	option(WITH_XML_BACKEND "Enable the serial backend" ON)

	if (WITH_XML_BACKEND)
		set(LIBIIO_CFILES ${LIBIIO_CFILES} xml.c)
		add_definitions(${LIBXML2_DEFINITIONS} -DXML_BACKEND=1)
		include_directories(${LIBXML2_INCLUDE_DIR})
		set(LIBS_TO_LINK ${LIBS_TO_LINK} ${LIBXML2_LIBRARIES})
	endif()
endif()

if (NEED_LIBXML2 AND NOT (LIBXML2_FOUND AND WITH_XML_BACKEND))
	message(SEND_ERROR "The selected backends require libxml2 and the XML backend to be enabled")
endif()

if (NEED_THREADS)
	if (NOT WIN32)
		find_library(PTHREAD_LIBRARIES pthread)

		if (PTHREAD_LIBRARIES)
			set(LIBS_TO_LINK ${LIBS_TO_LINK} ${PTHREAD_LIBRARIES})
		else()
			message(WARNING "pthread library not found; support for threads will be disabled")
			add_definitions(-DNO_THREADS=1)
		endif()
	else()
	endif()

	set(LIBIIO_CFILES ${LIBIIO_CFILES} lock.c)
endif()

if (IIOD_CLIENT)
	set(LIBIIO_CFILES ${LIBIIO_CFILES} iiod-client.c)
endif()

configure_file(libiio.iss.cmakein ${CMAKE_CURRENT_BINARY_DIR}/libiio.iss @ONLY)

set(LIBIIO_PC ${CMAKE_CURRENT_BINARY_DIR}/libiio.pc)
configure_file(libiio.pc.cmakein ${LIBIIO_PC} @ONLY)
install(FILES ${LIBIIO_PC} DESTINATION "${INSTALL_PKGCONFIG_DIR}")

#set(SETUP_PY ${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/setup.py)
#configure_file(python/setup.py.in ${SETUP_PY} @ONLY)

add_library(iio ${LIBIIO_CFILES} ${LIBIIO_HEADERS})
set_target_properties(iio PROPERTIES
	VERSION ${VERSION}
	SOVERSION ${LIBIIO_VERSION_MAJOR}
	FRAMEWORK TRUE
	PUBLIC_HEADER ${LIBIIO_HEADERS}
	C_STANDARD 99
	C_STANDARD_REQUIRED ON
	C_EXTENSIONS OFF
)
target_link_libraries(iio LINK_PRIVATE ${LIBS_TO_LINK})

if (MSVC)
	set_target_properties(iio PROPERTIES OUTPUT_NAME libiio)
endif()

if(NOT SKIP_INSTALL_ALL)
	install(TARGETS iio
		ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
		LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
		FRAMEWORK DESTINATION ${CMAKE_INSTALL_LIBDIR}
		PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

find_package(Doxygen)
if(DOXYGEN_FOUND)
	option(WITH_DOC "Generate documentation with Doxygen" ON)

	if (WITH_DOC)
		configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
		set(HTML_DEST_DIR ${CMAKE_CURRENT_BINARY_DIR}/html)
		file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/doc DESTINATION ${HTML_DEST_DIR})

		add_custom_command(TARGET iio POST_BUILD
			COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
			WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
			COMMENT "Generating API documentation with Doxygen" VERBATIM
			)

		install(DIRECTORY ${HTML_DEST_DIR} DESTINATION ${CMAKE_INSTALL_DOCDIR})
	endif()
else()
	message(STATUS "Doxygen not found, API documentation won't be generated")
endif()

# Create an installer if compiling for OSX
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set(LIBIIO_PKG ${CMAKE_CURRENT_BINARY_DIR}/libiio-${VERSION}.pkg)
	set(LIBIIO_TEMP_PKG ${CMAKE_CURRENT_BINARY_DIR}/libiio-${VERSION}-temp.pkg)
	set(LIBIIO_DISTRIBUTION_XML ${CMAKE_CURRENT_BINARY_DIR}/Distribution.xml)
	configure_file(Distribution.xml.cmakein ${LIBIIO_DISTRIBUTION_XML} @ONLY)

	find_program(PKGBUILD_EXECUTABLE
		NAMES pkgbuild
		DOC "OSX Package builder (pkgbuild)")
	mark_as_advanced(PKGBUILD_EXECUTABLE)

	find_program(PRODUCTBUILD_EXECUTABLE
		NAMES productbuild
		DOC "OSX Package builder (productbuild)")
	mark_as_advanced(PRODUCTBUILD_EXECUTABLE)

	add_custom_command(OUTPUT ${LIBIIO_TEMP_PKG} COMMAND ${PKGBUILD_EXECUTABLE}
			--root ${CMAKE_CURRENT_BINARY_DIR}/Debug
			--identifier libiio --version ${VERSION}
			--install-location /System/Library/Frameworks ${LIBIIO_TEMP_PKG}
		DEPENDS iio
	)

	add_custom_command(OUTPUT ${LIBIIO_PKG} COMMAND ${PRODUCTBUILD_EXECUTABLE}
			--distribution ${LIBIIO_DISTRIBUTION_XML} ${LIBIIO_PKG}
		DEPENDS ${LIBIIO_DISTRIBUTION_XML} ${LIBIIO_TEMP_PKG}
	)

	if (PKGBUILD_EXECUTABLE AND PRODUCTBUILD_EXECUTABLE)
		add_custom_target(libiio-pkg ALL DEPENDS ${LIBIIO_PKG})
	else()
		message(WARNING "Missing pkgbuild or productbuild: OSX installer won't be created.")
	endif()
endif()

if(WITH_IIOD)
	add_subdirectory(iiod)
endif()

if(WITH_TESTS)
	add_subdirectory(tests)
endif()

add_subdirectory(bindings)
