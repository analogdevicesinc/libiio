message(STATUS "cmake version: ${CMAKE_VERSION}")
cmake_minimum_required(VERSION 2.8.7)
project(libiio C)

if (MINGW)
	set(WIN32 ON)
endif()

set(LIBIIO_VERSION_MAJOR 0)
set(LIBIIO_VERSION_MINOR 23)
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

option(BUILD_SHARED_LIBS "Build shared libraries" ON)

if (NOT BUILD_SHARED_LIBS)
	add_definitions(-DLIBIIO_STATIC=1)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	option(OSX_PACKAGE "Create a OSX package" ON)

	set(OSX_INSTALL_FRAMEWORKSDIR "/Library/Frameworks" CACHE STRING "Installation directory for frameworks")
	get_filename_component(OSX_INSTALL_FRAMEWORKSDIR "${OSX_INSTALL_FRAMEWORKSDIR}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")

	set(CMAKE_MACOSX_RPATH ON)
	set(SKIP_INSTALL_ALL ${OSX_PACKAGE})
endif()

option(WITH_NETWORK_BACKEND "Enable the network backend" ON)
option(WITH_TESTS "Build the test programs" ON)
option(WITH_EXAMPLES "Build examples" OFF)

if (WITH_TESTS)
	set(NEED_THREADS 1)
endif()

if (NOT LOG_LEVEL)
	set(LOG_LEVEL Info CACHE STRING "Log level" FORCE)
	set_property(CACHE LOG_LEVEL PROPERTY STRINGS NoLog Error Warning Info Debug)
endif()

if (MSVC)
	add_compile_options(/W4 /wd4200 /wd4127 /wd4100)
	# C4200: nonstandard extension used : zero-sized array in struct (usb.h)
	# C4127: conditional expression is constant (IIO_ERROR and IIO_DEBUG macros)
	# C4100: unreferenced parameter; same as -Wno-unused-parameter

	set(CMAKE_FIND_LIBRARY_PREFIXES "lib" "")
	set(CMAKE_FIND_LIBRARY_SUFFIXES ".dll.a" ".a" ".lib")

elseif (CMAKE_COMPILER_IS_GNUCC)
	if (NOT WIN32)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
	endif()

	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter")

	include(CheckCCompilerFlag)
	check_c_compiler_flag(-Wpedantic HAS_WPEDANTIC)
	if (HAS_WPEDANTIC)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wpedantic")
	endif()
	check_c_compiler_flag(-Wshadow HAS_WSHADOW)
	if (HAS_WSHADOW)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wshadow")
	endif()
	# cmake 2.8 doesn't support C_STANDARD defined in set_target_properties
	if (${CMAKE_VERSION} VERSION_LESS "3.2")
		check_c_compiler_flag(-std=c99 HAS_C99)
		if (HAS_C99)
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
		endif()
	endif()

	# Per http://www.mingw.org/wiki/Use_more_recent_defined_functions
	if (MINGW)
		# Build for Vista and above
		# check mingw-w64-headers/include/w32api.h or windows sdkddkver.h
		# https://docs.microsoft.com/en-us/cpp/porting/modifying-winver-and-win32-winnt
		# using a hex value is bad - but stops from loading the otherwise unnecessary headers
		set(CMAKE_C_FLAGS "-D_WIN32_WINNT=0x600 ${CMAKE_C_FLAGS}")
		set(CMAKE_C_FLAGS "-DWINVER=0x600 ${CMAKE_C_FLAGS}")
	endif()
elseif (CMAKE_C_COMPILER_ID STREQUAL "Clang")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic -Wno-unused-parameter")
else()
	message(STATUS "Unknown compiler, please report upstream")
	message(STATUS "CMAKE_C_COMPILER_ID : " ${CMAKE_C_COMPILER_ID})
	message(STATUS "CFLAGS set to " ${CMAKE_C_FLAGS})
endif()

# based on host, set these on macos
if(APPLE)
	#full Single Unix Standard v3 (SUSv3) conformance (the Unix API)
	add_definitions(-D_DARWIN_C_SOURCE)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD|DragonFly|OpenBSD|NetBSD")
	set(CMAKE_REQUIRED_DEFINITIONS "-D__BSD_VISIBLE")
	add_definitions(-D__BSD_VISIBLE=1)
endif()
add_definitions(-D_POSIX_C_SOURCE=200809L -D__XSI_VISIBLE=500 -DLIBIIO_EXPORTS=1)

include(CheckSymbolExists)
check_symbol_exists(strdup "string.h" HAS_STRDUP)
check_symbol_exists(strndup "string.h" HAS_STRNDUP)
check_symbol_exists(strerror_r "string.h" HAS_STRERROR_R)
check_symbol_exists(newlocale "locale.h" HAS_NEWLOCALE)

option(ENABLE_IPV6 "Define if you want to enable IPv6 support" ON)
if (ENABLE_IPV6)
	check_symbol_exists(in6addr_any "netinet/in.h" HAVE_IPV6)
	if (NOT HAVE_IPV6)
		message(SEND_ERROR "IPv6 is not available in your system.")
	endif()
endif()

option(WITH_USB_BACKEND "Enable the libusb backend" ON)
if (WITH_USB_BACKEND)
	#Handle FreeBSD libusb and Linux libusb-1.0 libraries
	find_library(LIBUSB_LIBRARIES NAMES usb-1.0 usb)
	find_path(LIBUSB_INCLUDE_DIR libusb.h PATH_SUFFIXES libusb-1.0)
	if (NOT LIBUSB_LIBRARIES OR NOT LIBUSB_INCLUDE_DIR)
		message(SEND_ERROR "Unable to find libusb-1.0 dependency.\n"
			"If you want to disable the USB backend, set WITH_USB_BACKEND=OFF.")
	else()
		set(IIOD_CLIENT 1)
		set(NEED_LIBXML2 1)
		set(NEED_THREADS 1)

		include_directories(${LIBUSB_INCLUDE_DIR})

		set(TEMP ${CMAKE_REQUIRED_LIBRARIES})
		set(TEMP1 ${CMAKE_REQUIRED_INCLUDES})
		list(APPEND CMAKE_REQUIRED_LIBRARIES ${LIBUSB_LIBRARIES})
		list(APPEND CMAKE_REQUIRED_INCLUDES ${LIBUSB_INCLUDE_DIR})
		check_symbol_exists(libusb_get_version libusb.h HAS_LIBUSB_GETVERSION)
		set(CMAKE_REQUIRED_LIBRARIES ${TEMP})
		set(CMAKE_REQUIRED_INCLUDES ${TEMP1})
	endif()
endif()

# make sure all check_symbol_exists are before this point, otherwise they fail
# on some versions of compilers
if (MSVC)
	if(DEFINED ENV{CI} AND DEFINED ENV{APPVEYOR})
		message(STATUS "Running in an AppVeyor environment, setting -Werror")
		add_compile_options(/WX)
	endif()
elseif (CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
	option(WITH_GCOV "Build with gcov profiling flags" OFF)
	if (WITH_GCOV)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-arcs -ftest-coverage")
		SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -fprofile-arcs -ftest-coverage")
		SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}  -fprofile-arcs -ftest-coverage")
	endif()
	if(DEFINED ENV{CI} AND (DEFINED ENV{TRAVIS} OR DEFINED ENV{APPVEYOR}))
		message(STATUS "Running in CI environment (Travis or AppVeyor), setting -Werror")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")
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
endif()

set(LIBIIO_CFILES backend.c channel.c device.c context.c buffer.c utilities.c scan.c sort.c)
set(LIBIIO_HEADERS iio.h)

if(WITH_USB_BACKEND)
	list(APPEND LIBIIO_CFILES usb.c)
	list(APPEND LIBS_TO_LINK ${LIBUSB_LIBRARIES})
endif()

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

option(WITH_SERIAL_BACKEND "Enable the serial backend" OFF)
if (WITH_SERIAL_BACKEND)
	find_library(LIBSERIALPORT_LIBRARIES serialport)
	find_path(LIBSERIALPORT_INCLUDE_DIR libserialport.h)
	if (NOT LIBSERIALPORT_LIBRARIES OR NOT LIBSERIALPORT_INCLUDE_DIR)
		message(SEND_ERROR "Unable to find libserialport dependency.\n")
	else()
		message(STATUS "Looking for libserialport : Found")

		file(STRINGS ${LIBSERIALPORT_INCLUDE_DIR}/libserialport.h LIBSERIALPORT_VERSION_STR REGEX "SP_PACKAGE_VERSION_STRING")
		string(REGEX REPLACE "#define SP_PACKAGE_VERSION_STRING \"(.*)\"" "\\1" LIBSERIALPORT_VERSION ${LIBSERIALPORT_VERSION_STR})
		if ("${LIBSERIALPORT_VERSION}" VERSION_LESS 0.1.1)
			message(SEND_ERROR "The installed version of libserialport is too old. The minimum version supported is 0.1.1.")
		endif()

		list(APPEND LIBIIO_CFILES serial.c)
		list(APPEND LIBS_TO_LINK ${LIBSERIALPORT_LIBRARIES})

		set(NEED_THREADS 1)
		set(IIOD_CLIENT 1)
		set(NEED_LIBXML2 1)

		include_directories(${LIBSERIALPORT_INCLUDE_DIR})
	endif()
endif()

option(WITH_ZSTD "Support for ZSTD compressed metadata" OFF)
if (WITH_ZSTD)
	find_library(LIBZSTD_LIBRARIES zstd)
	find_path(LIBZSTD_INCLUDE_DIR zstd.h)

	if (NOT LIBZSTD_LIBRARIES OR NOT LIBZSTD_INCLUDE_DIR)
		message(SEND_ERROR "Unable to find libzstd dependency.\n"
			"If you want to disable ZSTD compression support, set WITH_ZSTD=OFF.")
	endif()

	list(APPEND LIBS_TO_LINK ${LIBZSTD_LIBRARIES})
	include_directories(${LIBZSTD_INCLUDE_DIR})
endif (WITH_ZSTD)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

if(WITH_NETWORK_BACKEND)
	if (WIN32)
		list(APPEND LIBS_TO_LINK wsock32 iphlpapi ws2_32)
	endif()

	if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
		include(CheckCSourceCompiles)

		option(WITH_NETWORK_GET_BUFFER "Enable experimental zero-copy transfers" OFF)
		if (WITH_NETWORK_GET_BUFFER)
			check_c_source_compiles("#define _GNU_SOURCE=1\n#include <fcntl.h>\nint main(void) { return O_TMPFILE; }"
				HAS_O_TMPFILE)

			if (NOT HAS_O_TMPFILE)
				message(SEND_ERROR "Zero-copy requires the O_TMPFILE flag, which is not available on the system.")
			endif()
		endif()

		check_c_source_compiles("#include <sys/eventfd.h>\nint main(void) { return eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK); }"
			WITH_NETWORK_EVENTFD)
		if (NOT WITH_NETWORK_EVENTFD)
			check_c_source_compiles("#define _GNU_SOURCE=1\n#include <unistd.h>\n#include <fcntl.h>\nint main(void) { int fd[2]; return pipe2(fd, O_CLOEXEC | O_NONBLOCK); }"
				HAS_PIPE2)
		endif()

		if (WITH_NETWORK_GET_BUFFER OR HAS_PIPE2)
			add_definitions(-D_GNU_SOURCE=1)
		endif()
	endif()

	list(APPEND LIBIIO_CFILES network.c)
	if (WIN32)
		list(APPEND LIBIIO_CFILES network-windows.c)
	else()
		list(APPEND LIBIIO_CFILES network-unix.c)
	endif()

	option(HAVE_DNS_SD "Enable DNS-SD (ZeroConf) support" ON)
	if (NOT HAVE_DNS_SD)
		message(STATUS "Building without DNS-SD (ZeroConf) support")
	elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
		find_library(CORE_SERVICES CoreServices)
		list(APPEND LIBIIO_CFILES dns_sd_bonjour.c dns_sd.c)
		list(APPEND LIBS_TO_LINK ${CORE_SERVICES} )
		set(HAVE_BONJOUR ON)
	elseif(WIN32)
		list(APPEND LIBIIO_CFILES dns_sd_windows.c dns_sd.c)
		if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
			set_source_files_properties(dns_sd_windows.c PROPERTIES COMPILE_FLAGS "-Wno-unused-function")
		endif()
	else()
		find_library(AVAHI_CLIENT_LIBRARIES avahi-client)
		find_library(AVAHI_COMMON_LIBRARIES avahi-common)
		if (NOT AVAHI_CLIENT_LIBRARIES OR NOT AVAHI_COMMON_LIBRARIES)
			message(SEND_ERROR "Unable to find libavahi-common / libavahi-client dependencies.\n"
				"If you want to disable DNS-SD (ZeroConf) support, set HAVE_DNS_SD=OFF.")
		endif()

		set(HAVE_AVAHI ON)
		list(APPEND LIBIIO_CFILES dns_sd_avahi.c dns_sd.c)
		set(AVAHI_LIBRARIES ${AVAHI_CLIENT_LIBRARIES} ${AVAHI_COMMON_LIBRARIES})
		list(APPEND LIBS_TO_LINK ${AVAHI_LIBRARIES})
	endif()

	set(NEED_THREADS 1)
	set(IIOD_CLIENT 1)
	set(NEED_LIBXML2 1)
else()
	set(HAVE_DNS_SD OFF)
endif()


option(WITH_XML_BACKEND "Enable the XML backend" ON)
if (WITH_XML_BACKEND)
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

	if (NOT LIBXML2_FOUND)
		message(SEND_ERROR "Unable to find libxml2 dependency.\n"
			"If you want to disable the XML backend, set WITH_XML_BACKEND=OFF.")
	endif()

	list(APPEND LIBIIO_CFILES xml.c)

	include_directories(${LIBXML2_INCLUDE_DIR})
	list(APPEND LIBS_TO_LINK ${LIBXML2_LIBRARIES})
elseif(NEED_LIBXML2)
	message(SEND_ERROR "Enabled backends require the XML backend to be enabled as well.\n"
		"If you want to enable the XML backend, set WITH_XML_BACKEND=ON.")
endif()

option(NO_THREADS "Disable multi-threading support" OFF)
if (NEED_THREADS)
	if (NOT NO_THREADS AND NOT WIN32 AND NOT ANDROID)
		find_library(PTHREAD_LIBRARIES pthread)

		if (NOT PTHREAD_LIBRARIES)
			message(SEND_ERROR "Unable to find pthread dependency.\n"
				"If you want to disable multi-threading support, set NO_THREADS=ON.")
		endif()

		list(APPEND LIBS_TO_LINK ${PTHREAD_LIBRARIES})
	endif()

	list(APPEND LIBIIO_CFILES lock.c)
endif()

if (IIOD_CLIENT)
	list(APPEND LIBIIO_CFILES iiod-client.c)
endif()

if (WIN32)
	string(REPLACE "." "," LIBIIO_FILEVERSION ${VERSION})
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
if(WITH_EXAMPLES)
	add_subdirectory(examples)
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
		${CMAKE_CURRENT_SOURCE_DIR}/bindings/csharp/Doxyfile.in
		${CMAKE_CURRENT_BINARY_DIR}/Doxyfile_csharp @ONLY)
	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/CI/travis/generateDocumentationAndDeploy.sh.in
		${CMAKE_CURRENT_BINARY_DIR}/generateDocumentationAndDeploy.sh @ONLY)
	file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/doc
		DESTINATION ${CMAKE_HTML_DEST_DIR}/${CMAKE_API_DEST_DIR}
		FILES_MATCHING PATTERN "*.svg")
	file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/doc/html/ DESTINATION ${CMAKE_HTML_DEST_DIR})
	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/doc/index.html.in
		${CMAKE_HTML_DEST_DIR}/index.html @ONLY)

	add_custom_command(TARGET iio POST_BUILD
		COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
		COMMENT "Generating API documentation with Doxygen" VERBATIM)
	add_custom_command(TARGET iio POST_BUILD
		COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile_csharp
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
		COMMENT "Generating C# documentation with Doxygen" VERBATIM)

	if(NOT SKIP_INSTALL_ALL)
		install(DIRECTORY ${CMAKE_HTML_DEST_DIR}
			DESTINATION ${CMAKE_INSTALL_DOCDIR})
	endif()
endif()

option(CSHARP_BINDINGS "Install C# bindings" OFF)
option(PYTHON_BINDINGS "Install Python bindings" OFF)
add_subdirectory(bindings)

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

	set(COPY_TOOLS_COMMAND)
	foreach(_tool ${IIO_TESTS_TARGETS})
		list(APPEND COPY_TOOLS_COMMAND
			COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${_tool}> ${LIBIIO_FRAMEWORK_DIR}/Tools)
	endforeach()

	add_custom_command(OUTPUT ${LIBIIO_PKG}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBIIO_FRAMEWORK_DIR}/Tools
		${COPY_TOOLS_COMMAND}
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
		message(SEND_ERROR "IIOD requires pthread support\n."
			"If you want to disable IIOD, set WITH_IIOD=OFF.")
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

list(APPEND IIO_FEATURES_${WITH_XML_BACKEND} xml)
list(APPEND IIO_FEATURES_${WITH_ZSTD} zstd)
list(APPEND IIO_FEATURES_${WITH_NETWORK_BACKEND} network)
list(APPEND IIO_FEATURES_${HAVE_DNS_SD} dns-sd)
list(APPEND IIO_FEATURES_${HAVE_AVAHI} avahi)
list(APPEND IIO_FEATURES_${HAVE_BONJOUR} bonjour)
list(APPEND IIO_FEATURES_${ENABLE_IPV6} ipv6)
list(APPEND IIO_FEATURES_${WITH_SERIAL_BACKEND} serial)
list(APPEND IIO_FEATURES_${WITH_LOCAL_BACKEND} local)
list(APPEND IIO_FEATURES_${WITH_USB_BACKEND} usb)
list(APPEND IIO_FEATURES_${WITH_TESTS} utils)
list(APPEND IIO_FEATURES_${WITH_EXAMPLES} examples)
list(APPEND IIO_FEATURES_${WITH_IIOD} iiod)
#add iiod settings
list(APPEND IIO_FEATURES_ON ${IIOD_FEATURES_ON})
list(APPEND IIO_FEATURES_OFF ${IIOD_FEATURES_OFF})
list(APPEND IIO_FEATURES_${WITH_DOC} doc)
#add man page settings
list(APPEND IIO_FEATURES_ON ${IIOM_FEATURES_ON})
list(APPEND IIO_FEATURES_OFF ${IIOM_FEATURES_OFF})
#add binding settings
list(APPEND IIO_FEATURES_ON ${IIOB_FEATURES_ON})
list(APPEND IIO_FEATURES_OFF ${IIOB_FEATURES_OFF})

string(REPLACE ";" " " IIO_FEATURES_ON "${IIO_FEATURES_ON}")
string(REPLACE ";" " " IIO_FEATURES_OFF "${IIO_FEATURES_OFF}")

message(STATUS "Features enabled : ${IIO_FEATURES_ON}")
message(STATUS "Features disabled: ${IIO_FEATURES_OFF}")
