cmake_minimum_required(VERSION 2.8.7)
project(libiio C)

set(LIBIIO_VERSION_MAJOR 0)
set(LIBIIO_VERSION_MINOR 19)
set(VERSION "${LIBIIO_VERSION_MAJOR}.${LIBIIO_VERSION_MINOR}")
if (WIN32)
	string(TIMESTAMP BUILD_YEAR "%Y")
endif()

# Set the default install path to /usr
if (NOT WIN32 AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "default install path" FORCE)
endif()

set(CMAKE_INSTALL_DOCDIR "" CACHE PATH "documentation root (DATAROOTDIR/doc/${PROJECT_NAME}${LIBIIO_VERSION_MAJOR}-doc)")
include(GNUInstallDirs)
if(ENABLE_SHARED AND ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}")
	set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()
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

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	option(OSX_PACKAGE "Create a OSX package" ON)

	set(OSX_INSTALL_FRAMEWORKSDIR "/Library/Frameworks" CACHE STRING "Installation directory for frameworks")
	get_filename_component(OSX_INSTALL_FRAMEWORKSDIR "${OSX_INSTALL_FRAMEWORKSDIR}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")

	set(CMAKE_MACOSX_RPATH ON)
	set(SKIP_INSTALL_ALL ${OSX_PACKAGE})
endif()

option(WITH_NETWORK_BACKEND "Enable the network backend" ON)
option(WITH_TESTS "Build the test programs" ON)

if (WITH_TESTS)
	set(NEED_THREADS 1)
endif()

if (MSVC)
	# Avoid annoying warnings from Visual Studio
	add_definitions(-D_CRT_SECURE_NO_WARNINGS=1)

	set(CMAKE_FIND_LIBRARY_PREFIXES "lib" "")
	set(CMAKE_FIND_LIBRARY_SUFFIXES ".dll.a" ".a" ".lib")
endif()

if (NOT LOG_LEVEL)
	set(LOG_LEVEL Info CACHE STRING "Log level" FORCE)
	set_property(CACHE LOG_LEVEL PROPERTY STRINGS NoLog Error Warning Info Debug)
endif()

if (CMAKE_COMPILER_IS_GNUCC)
	if (NOT WIN32)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
	endif()

	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare")

	include(CheckCCompilerFlag)
	check_c_compiler_flag(-Wpedantic HAS_WPEDANTIC)
	if (HAS_WPEDANTIC)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wpedantic")
	endif()
endif()

if(APPLE)
	#full Single Unix Standard v3 (SUSv3) conformance (the Unix API)
	add_definitions(-D_DARWIN_C_SOURCE)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic -Wno-unused-parameter -Wno-sign-compare")
endif()

include(CheckSymbolExists)
check_symbol_exists(strdup "string.h" HAS_STRDUP)
check_symbol_exists(strerror_r "string.h" HAS_STRERROR_R)
check_symbol_exists(newlocale "locale.h" HAS_NEWLOCALE)

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

# For BSD variants
if(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD|DragonFly|OpenBSD|NetBSD")
	set(CMAKE_REQUIRED_DEFINITIONS "-D__BSD_VISIBLE")
	add_definitions(-D__BSD_VISIBLE=1)
endif()

option(ENABLE_IPV6 "Define if you want to enable IPv6 support" ON)
if (ENABLE_IPV6)
	check_symbol_exists(in6addr_any "netinet/in.h" HAVE_IPV6)

	if (NOT HAVE_IPV6)
		message(WARNING "IPv6 is not available in your system.")
	endif()
endif()

set(LIBIIO_CFILES backend.c channel.c device.c context.c buffer.c utilities.c scan.c sort.c)
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
		ERROR_QUIET
	)

	if ("${LIBIIO_GIT_REPO}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
		execute_process(
			COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
			OUTPUT_VARIABLE LIBIIO_VERSION_GIT
			OUTPUT_STRIP_TRAILING_WHITESPACE
		)
	endif()
endif()

set(LIBIIO_VERSION  ${VERSION}.g${LIBIIO_VERSION_GIT})
file(WRITE ${CMAKE_BINARY_DIR}/.version ${LIBIIO_VERSION})

if (NOT LIBIIO_VERSION_GIT)
	set(LIBIIO_VERSION_GIT v${VERSION})
endif()

if(WITH_LOCAL_BACKEND)
	add_definitions(-DLOCAL_BACKEND=1)
	list(APPEND LIBIIO_CFILES local.c)

	# Link with librt if present
	find_library(LIBRT_LIBRARIES rt)
	if (LIBRT_LIBRARIES)
		list(APPEND LIBS_TO_LINK ${LIBRT_LIBRARIES})
	endif()

	option(WITH_LOCAL_CONFIG "Read local context attributes from /etc/libiio.ini" ON)
	if (WITH_LOCAL_CONFIG)
		list(APPEND LIBIIO_CFILES ./libini/libini.c)
	endif()
endif()

#Handle FreeBSD libusb and Linux libusb-1.0 libraries
find_library(LIBUSB_LIBRARIES NAMES usb-1.0 usb)
find_path(LIBUSB_INCLUDE_DIR libusb.h PATH_SUFFIXES libusb-1.0)
if (LIBUSB_LIBRARIES AND LIBUSB_INCLUDE_DIR)
	message(STATUS "Looking for libusb-1.0 : Found")
	option(WITH_USB_BACKEND "Enable the libusb backend" ON)

	if(WITH_USB_BACKEND)
		add_definitions(-DUSB_BACKEND=1)
		list(APPEND LIBIIO_CFILES usb.c)
		list(APPEND LIBS_TO_LINK ${LIBUSB_LIBRARIES})
		set(IIOD_CLIENT 1)
		set(NEED_LIBXML2 1)
		set(NEED_THREADS 1)

		include_directories(${LIBUSB_INCLUDE_DIR})

		set(TEMP ${CMAKE_REQUIRED_LIBRARIES})
		set(TEMP1 ${CMAKE_REQUIRED_INCLUDES})
		list(APPEND CMAKE_REQUIRED_LIBRARIES ${LIBUSB_LIBRARIES})
		list(APPEND CMAKE_REQUIRED_INCLUDES ${LIBUSB_INCLUDE_DIR})
		check_symbol_exists(libusb_get_version libusb.h
			HAS_LIBUSB_GETVERSION)
		set(CMAKE_REQUIRED_LIBRARIES ${TEMP})
		set(CMAKE_REQUIRED_INCLUDES ${TEMP1})
	endif()
else()
	message(STATUS "Looking for libusb-1.0 : Failed; building without usb")
endif()

find_library(LIBSERIALPORT_LIBRARIES serialport)
find_path(LIBSERIALPORT_INCLUDE_DIR libserialport.h)
if (LIBSERIALPORT_LIBRARIES AND LIBSERIALPORT_INCLUDE_DIR)
	option(WITH_SERIAL_BACKEND "Enable the serial backend" ON)

	if (WITH_SERIAL_BACKEND)
		file(STRINGS ${LIBSERIALPORT_INCLUDE_DIR}/libserialport.h LIBSERIALPORT_VERSION_STR REGEX "SP_PACKAGE_VERSION_STRING")
		string(REGEX REPLACE "#define SP_PACKAGE_VERSION_STRING \"(.*)\"" "\\1" LIBSERIALPORT_VERSION ${LIBSERIALPORT_VERSION_STR})
		if ("${LIBSERIALPORT_VERSION}" VERSION_LESS 0.1.1)
			message(WARNING "The installed version of libserialport is too old. The minimum version supported is 0.1.1. Disabling Serial support.")
			SET(WITH_SERIAL_BACKEND OFF)
		else()
			add_definitions(-DSERIAL_BACKEND=1)
			list(APPEND LIBIIO_CFILES serial.c)
			list(APPEND LIBS_TO_LINK ${LIBSERIALPORT_LIBRARIES})

			set(NEED_THREADS 1)
			set(IIOD_CLIENT 1)
			set(NEED_LIBXML2 1)

			include_directories(${LIBSERIALPORT_INCLUDE_DIR})
		endif()
	endif()
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

if(WITH_NETWORK_BACKEND)
	message(STATUS "Building with Network back end support")
	if (WIN32)
		list(APPEND LIBS_TO_LINK wsock32 ws2_32)
	endif()

	if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
		include(CheckCSourceCompiles)
		check_c_source_compiles("#include <fcntl.h>\nint main(void) { return O_TMPFILE; }" HAS_O_TMPFILE)

		if (HAS_O_TMPFILE)
			option(WITH_NETWORK_GET_BUFFER "Enable experimental zero-copy transfers" OFF)
		endif(HAS_O_TMPFILE)

		check_c_source_compiles("#include <sys/eventfd.h>\nint main(void) { return eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK); }" WITH_NETWORK_EVENTFD)
	endif()

	if(NOT WIN32)
		include(CheckCSourceCompiles)
		check_c_source_compiles("#include <unistd.h>\n#include <fcntl.h>\nint main(void) { int fd[2]; return pipe2(fd, O_CLOEXEC | O_NONBLOCK); }" HAS_PIPE2)
	endif()

	add_definitions(-DNETWORK_BACKEND=1)
	list(APPEND LIBIIO_CFILES network.c)

	find_library(AVAHI_CLIENT_LIBRARIES avahi-client)
	find_library(AVAHI_COMMON_LIBRARIES avahi-common)
	if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
		find_library(CORE_SERVICES CoreServices)

		message(STATUS "Building with CFNetServices, an Apple DNS SD implementation")
		set(HAVE_DNS_SD ON)

		list(APPEND LIBIIO_CFILES dns_sd_bonjour.c dns_sd.c)
		list(APPEND LIBS_TO_LINK ${CORE_SERVICES} )

	elseif(AVAHI_CLIENT_LIBRARIES AND AVAHI_COMMON_LIBRARIES)
		message(STATUS "Building with Avahi, a DNS SD implementation")
		set(HAVE_DNS_SD ON)
		set(HAVE_AVAHI ON)
		
		list(APPEND LIBIIO_CFILES dns_sd_avahi.c dns_sd.c)
		set(AVAHI_LIBRARIES ${AVAHI_CLIENT_LIBRARIES} ${AVAHI_COMMON_LIBRARIES})
		list(APPEND LIBS_TO_LINK ${AVAHI_LIBRARIES})
	else()
		message(STATUS "Building without DNS-SD (Zeroconf) support")
	endif()

	set(NEED_THREADS 1)
	set(IIOD_CLIENT 1)
	set(NEED_LIBXML2 1)
else()
	message(STATUS "Building without network support")
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
	option(WITH_XML_BACKEND "Enable the XML backend" ON)

	if (WITH_XML_BACKEND)
		list(APPEND LIBIIO_CFILES xml.c)

		add_definitions(${LIBXML2_DEFINITIONS} -DXML_BACKEND=1)
		include_directories(${LIBXML2_INCLUDE_DIR})
		list(APPEND LIBS_TO_LINK ${LIBXML2_LIBRARIES})
	endif()
endif()

if (NEED_LIBXML2 AND NOT (LIBXML2_FOUND AND WITH_XML_BACKEND))
	message(SEND_ERROR "The selected backends require libxml2 and the XML backend to be enabled")
endif()

if (NEED_THREADS)
	if (NOT WIN32)
		find_library(PTHREAD_LIBRARIES pthread)

		if (PTHREAD_LIBRARIES)
			list(APPEND LIBS_TO_LINK ${PTHREAD_LIBRARIES})
		else()
			message(WARNING "pthread library not found; support for threads will be disabled")
			set(NO_THREADS ON)
		endif()
	else()
	endif()

	list(APPEND LIBIIO_CFILES lock.c)
endif()

if (IIOD_CLIENT)
	list(APPEND LIBIIO_CFILES iiod-client.c)
endif()

configure_file(libiio.iss.cmakein ${CMAKE_CURRENT_BINARY_DIR}/libiio.iss @ONLY)

set(LIBIIO_PC ${CMAKE_CURRENT_BINARY_DIR}/libiio.pc)
configure_file(libiio.pc.cmakein ${LIBIIO_PC} @ONLY)

if(NOT SKIP_INSTALL_ALL)
	install(FILES ${LIBIIO_PC} DESTINATION "${INSTALL_PKGCONFIG_DIR}")
endif()

if(WITH_TESTS)
	add_subdirectory(tests)
endif()

if (WIN32)
	set(LIBIIO_ORIGINAL_FILENAME libiio.dll)
	set(LIBIIO_RC ${CMAKE_CURRENT_BINARY_DIR}/properties.rc)
	configure_file(properties.rc.cmakein ${LIBIIO_RC} @ONLY)
endif()

add_library(iio ${LIBIIO_CFILES} ${LIBIIO_HEADERS} ${LIBIIO_EXTRA_HEADERS} ${LIBIIO_RC})
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
		FRAMEWORK DESTINATION ${OSX_INSTALL_FRAMEWORKSDIR}
		PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

option(WITH_DOC "Generate documentation with Doxygen" OFF)
if(WITH_DOC)
	find_package(Doxygen REQUIRED)
	# It is not an error when 'dot' is not found,
	# just switching off the Doxygen's HAVE_DOT option
	find_package_handle_standard_args(Dot REQUIRED_VARS DOXYGEN_DOT_EXECUTABLE)

	include(cmake/CheckCaseSensitiveFileSystem.cmake)
	if (HAVE_CASE_SENSITIVE_FILESYSTEM)
		set(CMAKE_CASE_SENSITIVE_FILESYSTEM "YES")
	else()
		set(CMAKE_CASE_SENSITIVE_FILESYSTEM "NO")
	endif()

	set(CMAKE_HTML_DEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/html/v${LIBIIO_VERSION_MAJOR}.${LIBIIO_VERSION_MINOR}")
	set(CMAKE_API_DEST_DIR "${PROJECT_NAME}")

	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
		${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/CI/travis/generateDocumentationAndDeploy.sh.in
		${CMAKE_CURRENT_BINARY_DIR}/generateDocumentationAndDeploy.sh @ONLY)
	file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/doc
		DESTINATION ${CMAKE_HTML_DEST_DIR}/${CMAKE_API_DEST_DIR}
		FILES_MATCHING PATTERN "*.svg")
	file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/doc/html/ DESTINATION ${CMAKE_HTML_DEST_DIR})

	add_custom_command(TARGET iio POST_BUILD
		COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
		COMMENT "Generating API documentation with Doxygen" VERBATIM)

	if(NOT SKIP_INSTALL_ALL)
		install(DIRECTORY ${HTML_DEST_DIR}
			DESTINATION ${CMAKE_INSTALL_DOCDIR})
	endif()
endif()

option(CSHARP_BINDINGS "Install C# bindings" OFF)
option(PYTHON_BINDINGS "Install Python bindings" OFF)
option(MATLAB_BINDINGS "Build MATLAB bindings" OFF)
add_subdirectory(bindings)

if (WITH_MATLAB_BINDINGS_API)
	list(APPEND LIBIIO_EXTRA_HEADERS bindings/matlab/iio-wrapper.h)
	add_definitions(-DMATLAB_BINDINGS_API=1)
endif()

option(WITH_MAN "Generate on-line reference manuals (man pages)" OFF)
add_subdirectory(man)

# Create an installer if compiling for OSX
if(OSX_PACKAGE)
	set(LIBIIO_PKG ${CMAKE_CURRENT_BINARY_DIR}/libiio-${VERSION}.g${LIBIIO_VERSION_GIT}.pkg)
	set(LIBIIO_TEMP_PKG ${CMAKE_CURRENT_BINARY_DIR}/libiio-${VERSION}-temp.pkg)
	set(LIBIIO_DISTRIBUTION_XML ${CMAKE_CURRENT_BINARY_DIR}/Distribution.xml)
	set(LIBIIO_FRAMEWORK_DIR ${CMAKE_CURRENT_BINARY_DIR}/iio.framework)
	configure_file(Distribution.xml.cmakein ${LIBIIO_DISTRIBUTION_XML} @ONLY)

	find_program(PKGBUILD_EXECUTABLE
		NAMES pkgbuild
		DOC "OSX Package builder (pkgbuild)")
	mark_as_advanced(PKGBUILD_EXECUTABLE)

	find_program(PRODUCTBUILD_EXECUTABLE
		NAMES productbuild
		DOC "OSX Package builder (productbuild)")
	mark_as_advanced(PRODUCTBUILD_EXECUTABLE)

	foreach(_tool ${IIO_TESTS_TARGETS})
		list(APPEND IIO_TESTS $<TARGET_FILE:${_tool}>)
	endforeach()

	add_custom_command(OUTPUT ${LIBIIO_PKG}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBIIO_FRAMEWORK_DIR}/Tools
		COMMAND ${CMAKE_COMMAND} -E copy ${IIO_TESTS} ${LIBIIO_FRAMEWORK_DIR}/Tools
		COMMAND ${PKGBUILD_EXECUTABLE}
			--component ${LIBIIO_FRAMEWORK_DIR}
			--identifier com.adi.iio --version ${VERSION}
			--install-location ${OSX_INSTALL_FRAMEWORKSDIR} ${LIBIIO_TEMP_PKG}
		COMMAND ${PRODUCTBUILD_EXECUTABLE}
			--distribution ${LIBIIO_DISTRIBUTION_XML} ${LIBIIO_PKG}
		COMMAND ${CMAKE_COMMAND} -E remove ${LIBIIO_TEMP_PKG}
		DEPENDS iio ${IIO_TESTS_TARGETS} ${LIBIIO_DISTRIBUTION_XML}
	)

	if (PKGBUILD_EXECUTABLE AND PRODUCTBUILD_EXECUTABLE)
		add_custom_target(libiio-pkg ALL DEPENDS ${LIBIIO_PKG})

		install(CODE "execute_process(COMMAND /usr/sbin/installer -pkg ${LIBIIO_PKG} -target /)")
	else()
		message(WARNING "Missing pkgbuild or productbuild: OSX installer won't be created.")
	endif()
endif()

if(WITH_IIOD)
	option(WITH_SYSTEMD "Enable installation of systemd service file for iiod" OFF)
	set(SYSTEMD_UNIT_INSTALL_DIR /lib/systemd/system CACHE PATH "default install path for systemd unit files")

	option(WITH_SYSVINIT "Enable installation of SysVinit script for iiod" OFF)
	set(SYSVINIT_INSTALL_DIR /etc/init.d CACHE PATH "default install path for SysVinit scripts")

	option(WITH_UPSTART "Enable installation of upstart config file for iiod" OFF)
	set(UPSTART_CONF_INSTALL_DIR /etc/init CACHE PATH "default install path for upstart conf files")

	if (NOT PTHREAD_LIBRARIES)
		message(WARNING "IIOD requires threads support; disabling")
		set(WITH_IIOD OFF CACHE BOOL "" FORCE)
	else()
		add_subdirectory(iiod)
	endif()
endif()

if (NOT OSX_PACKAGE)
	# Support creating some basic binpkgs via `make package`.
	# Disabled if OSX_PACKAGE is enabled, as tarballs would end up empty otherwise.
	option(ENABLE_PACKAGING "Create .deb/.rpm or .tar.gz packages via 'make package'" OFF)

	if(ENABLE_PACKAGING)
		if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
			include(cmake/DarwinPackaging.cmake)
		endif()
		if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
			include(cmake/LinuxPackaging.cmake)
		endif()
	endif()
endif()

if (WITH_USB_BACKEND AND CMAKE_SYSTEM_NAME MATCHES "^Linux")
	option(INSTALL_UDEV_RULE "Install a udev rule for detection of USB devices" ON)

	if (INSTALL_UDEV_RULE)
		set(UDEV_RULES_INSTALL_DIR /lib/udev/rules.d CACHE PATH "default install path for udev rules")

		configure_file(libiio.rules.cmakein ${CMAKE_CURRENT_BINARY_DIR}/90-libiio.rules @ONLY)
		install(FILES ${CMAKE_CURRENT_BINARY_DIR}/90-libiio.rules DESTINATION ${UDEV_RULES_INSTALL_DIR})
	endif()
endif()

configure_file(iio-config.h.cmakein ${CMAKE_CURRENT_BINARY_DIR}/iio-config.h @ONLY)
