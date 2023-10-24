message(STATUS "cmake version: ${CMAKE_VERSION}")
cmake_minimum_required(VERSION 3.10)

cmake_policy(SET CMP0048 NEW) # project() sets the VERSION variables
project(libiio LANGUAGES C VERSION 1.0)

# Set the default install path to /usr
if (NOT WIN32 AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "default install path" FORCE)
endif()

if (MINGW)
	set(WIN32 ON)
endif()

set(LIBIIO_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(LIBIIO_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(VERSION "${PROJECT_VERSION}")
if (WIN32)
	string(TIMESTAMP BUILD_YEAR "%Y")
	string(REPLACE "." "," LIBIIO_FILEVERSION ${VERSION})
endif()

if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
		"Choose the type of build, options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release RelWithDebInfo MinSizeRel."
		FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS None Debug Release RelWithDebInfo MinSizeRel)
endif()

option(BUILD_SHARED_LIBS "Build shared libraries" ON)

add_library(iio
	attr.c
	backend.c
	block.c
	buffer.c
	channel.c
	context.c
	device.c
	events.c
	mask.c
	scan.c
	sort.c
	stream.c
	task.c
	utilities.c
	${CMAKE_CURRENT_BINARY_DIR}/iio-config.h
)

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|MSVC")
	option(LIBIIO_COMPAT "Add compatibility layer for libiio 0.x" ON)
	if (LIBIIO_COMPAT)
		add_library(iio-compat compat.c)
		if (WIN32)
			target_sources(iio-compat PRIVATE dynamic-windows.c)
			target_compile_definitions(iio-compat PRIVATE
				LIBIIO1_NAME="iio${PROJECT_VERSION_MAJOR}.dll"
			)
		else()
			target_sources(iio-compat PRIVATE dynamic-unix.c)
			target_link_libraries(iio-compat PRIVATE dl)
			target_compile_definitions(iio-compat PRIVATE
				LIBIIO1_NAME="libiio.so.${PROJECT_VERSION_MAJOR}"
			)
		endif()

		target_include_directories(iio-compat PUBLIC ${CMAKE_BINARY_DIR} include)
		target_compile_definitions(iio-compat PRIVATE LIBIIO_COMPAT_EXPORTS)
		set(IIO_COMPAT_LIB iio-compat)

		set_target_properties(iio-compat PROPERTIES
			OUTPUT_NAME iio
			NO_SONAME ON
			VERSION 0.99
			SOVERSION 0
		)
		if (NOT WIN32)
			set_target_properties(iio-compat PROPERTIES SUFFIX .so.0)
		endif()
	endif()
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	option(OSX_FRAMEWORK "Create a OSX_FRAMEWORK" ON)
else()
	set(OSX_FRAMEWORK OFF)
endif()

set(IIO_PUBLIC_HEADERS include/iio/iio.h;include/iio/iio-debug.h;include/iio/iio-lock.h;include/iio/iiod-client.h)

set_target_properties(iio PROPERTIES
	VERSION ${PROJECT_VERSION}
	SOVERSION ${PROJECT_VERSION_MAJOR}
	PUBLIC_HEADER "${IIO_PUBLIC_HEADERS}"
)
set_target_properties(iio ${IIO_COMPAT_LIB} PROPERTIES
	FRAMEWORK ${OSX_FRAMEWORK}
	C_STANDARD 99
	C_STANDARD_REQUIRED ON
	C_EXTENSIONS OFF
)
if (WIN32)
	if (MSVC)
		target_link_options(iio PUBLIC /DEBUG)
		set(LIBIIO_WIN32_PREFIX lib)
	endif()

	# On Windows, DLLs aren't suffixed with the library's version;
	# therefore the libiio 1.x DLL needs to have a different name
	# than the compatibility library.
	set_target_properties(iio PROPERTIES
		OUTPUT_NAME ${LIBIIO_WIN32_PREFIX}iio${PROJECT_VERSION_MAJOR}
	)
endif()

target_include_directories(iio PUBLIC include ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})

target_compile_definitions(iio PRIVATE LIBIIO_EXPORTS)

target_compile_definitions(iio PUBLIC
	# Request Posix 2008.09
	_POSIX_C_SOURCE=200809L __XSI_VISIBLE=500

	# Per http://www.mingw.org/wiki/Use_more_recent_defined_functions
	$<$<PLATFORM_ID:MinGW>:_WIN32_WINNT=0x600 WINVER=0x600>

	# full Single Unix Standard v3 (SUSv3) conformance (the Unix API)
	$<$<PLATFORM_ID:Darwin>:_DARWIN_C_SOURCE>

	# Set -DLIBIIO_STATIC if building statically
	$<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:LIBIIO_STATIC>
)

if (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD|DragonFly|OpenBSD|NetBsd")
	target_compile_definitions(iio PUBLIC __BSD_VISIBLE)
	set(CMAKE_REQUIRED_DEFINITIONS -D__BSD_VISIBLE=1)
endif()

include(GNUInstallDirs)

if (BUILD_SHARED_LIBS)
	option(WITH_MODULES "Enable support for dynamically-loaded modules" OFF)
	if (WITH_MODULES)
		target_sources(iio PRIVATE dynamic.c)
		if (WIN32)
			target_sources(iio PRIVATE dynamic-windows.c)
		else()
			target_sources(iio PRIVATE dynamic-unix.c)
			target_link_libraries(iio PRIVATE dl)
		endif()

		file(TO_NATIVE_PATH "${CMAKE_INSTALL_FULL_LIBDIR}/libiio/" IIO_MODULES_DIR)
		string(REPLACE "\\" "\\\\" IIO_MODULES_DIR ${IIO_MODULES_DIR})
	endif()
endif()

option(WITH_EXAMPLES "Build examples" OFF)
option(WITH_UTILS "Build the Libiio utility programs" ON)

if (NOT LOG_LEVEL)
	set(LOG_LEVEL Info CACHE STRING "Log level" FORCE)
	set_property(CACHE LOG_LEVEL PROPERTY STRINGS NoLog Error Warning Info Debug)
endif()

set(LEVEL_NoLog 1)
set(LEVEL_Error 2)
set(LEVEL_Warning 3)
set(LEVEL_Info 4)
set(LEVEL_Debug 5)
set(DEFAULT_LOG_LEVEL ${LEVEL_${LOG_LEVEL}})

if (MSVC)
	target_compile_options(iio PRIVATE /Zi /W4 /wd4200 /wd4127 /wd4100)
	# Zi produces a separate PDB file that contains all the symbolic debugging information
	# W4 displays level 1, 2, 3, and 4 (informational) warnings that aren't off by default
	# C4200: nonstandard extension used : zero-sized array in struct (usb.h)
	# C4127: conditional expression is constant (IIO_ERROR and IIO_DEBUG macros)
	# C4100: unreferenced parameter; same as -Wno-unused-parameter

	set(CMAKE_FIND_LIBRARY_PREFIXES "lib" "")
	set(CMAKE_FIND_LIBRARY_SUFFIXES ".dll.a" ".a" ".lib")

elseif (CMAKE_COMPILER_IS_GNUCC)
	if (NOT WIN32)
		target_compile_options(iio PUBLIC -fvisibility=hidden)
	endif()

	include(CheckCCompilerFlag)
	check_c_compiler_flag(-Wpedantic HAS_WPEDANTIC)
	check_c_compiler_flag(-Wshadow HAS_WSHADOW)

	target_compile_options(iio PRIVATE -Wall -Wextra -Wno-unused-parameter
		$<$<BOOL:HAS_WPEDANTIC>:-Wpedantic>
		$<$<BOOL:HAS_WSHADOW>:-Wshadow>
	)

elseif (CMAKE_C_COMPILER_ID STREQUAL "Clang")
	target_compile_options(iio PRIVATE -Wall -Wextra -pedantic -Wno-unused-parameter)
else()
	message(STATUS "Unknown compiler, please report upstream")
	message(STATUS "CMAKE_C_COMPILER_ID : " ${CMAKE_C_COMPILER_ID})
	message(STATUS "CFLAGS set to " ${CMAKE_C_FLAGS})
endif()

include(CheckSymbolExists)
check_symbol_exists(strdup "string.h" HAS_STRDUP)
check_symbol_exists(strndup "string.h" HAS_STRNDUP)
check_symbol_exists(strerror_r "string.h" HAS_STRERROR_R)
check_symbol_exists(strtok_r "string.h" HAS_STRTOK_R)
check_symbol_exists(newlocale "locale.h" HAS_NEWLOCALE)

option(ENABLE_IPV6 "Define if you want to enable IPv6 support" ON)
if (ENABLE_IPV6)
	if (WIN32)
		set(HAVE_IPV6 ON)
	else()
		check_symbol_exists(in6addr_any "netinet/in.h" HAVE_IPV6)
	endif()
	if (NOT HAVE_IPV6)
		message(SEND_ERROR "IPv6 is not available in your system.")
	endif()
endif()

option(WITH_USB_BACKEND "Enable the libusb backend" ON)
if (WITH_USB_BACKEND)
	find_package(PkgConfig)
	if (PkgConfig_FOUND)
		pkg_check_modules(LIBUSB libusb-1.0)
	endif()
	if (LIBUSB_LINK_LIBRARIES AND LIBUSB_INCLUDE_DIRS)
		set(LIBUSB_LIBRARIES ${LIBUSB_LINK_LIBRARIES})
		set(LIBUSB_INCLUDE_DIR ${LIBUSB_INCLUDE_DIRS})
	else()
		#Handle FreeBSD libusb and Linux libusb-1.0 libraries
		find_library(LIBUSB_LIBRARIES NAMES usb-1.0 usb)
		find_path(LIBUSB_INCLUDE_DIR libusb.h PATH_SUFFIXES libusb-1.0)
	endif()
	if (NOT LIBUSB_LIBRARIES OR NOT LIBUSB_INCLUDE_DIR)
		message(SEND_ERROR "Unable to find libusb-1.0 dependency.\n"
			"If you want to disable the USB backend, set WITH_USB_BACKEND=OFF.")
	else()
		list(APPEND LIBIIO_SCAN_BACKENDS usb)
		set(IIOD_CLIENT 1)
		set(NEED_LIBXML2 1)

		if (WITH_MODULES)
			option(WITH_USB_BACKEND_DYNAMIC "Build USB backend as a module instead of built-in" ON)
		else()
			set(WITH_USB_BACKEND_DYNAMIC OFF)
		endif()
		if (WITH_USB_BACKEND_DYNAMIC)
			add_library(iio-usb SHARED usb.c)
			target_link_libraries(iio-usb PRIVATE iio ${LIBUSB_LIBRARIES})
			target_include_directories(iio-usb PRIVATE ${LIBUSB_INCLUDE_DIR})
			set_target_properties(iio-usb PROPERTIES PREFIX lib)
		else()
			target_include_directories(iio PRIVATE ${LIBUSB_INCLUDE_DIR})
			target_sources(iio PRIVATE usb.c)
			target_link_libraries(iio PRIVATE ${LIBUSB_LIBRARIES})
		endif()

		set(TEMP ${CMAKE_REQUIRED_LIBRARIES})
		set(TEMP1 ${CMAKE_REQUIRED_INCLUDES})
		list(APPEND CMAKE_REQUIRED_LIBRARIES ${LIBUSB_LIBRARIES})
		list(APPEND CMAKE_REQUIRED_INCLUDES ${LIBUSB_INCLUDE_DIR})
		check_symbol_exists(libusb_get_version libusb.h HAS_LIBUSB_GETVERSION)
		set(CMAKE_REQUIRED_LIBRARIES ${TEMP})
		set(CMAKE_REQUIRED_INCLUDES ${TEMP1})

		if (NOT HAS_LIBUSB_GETVERSION)
			message(SEND_ERROR "Libusb >= 1.0.10 is required.")
		endif()
	endif()
endif()

# make sure all check_symbol_exists are before this point, otherwise they fail
# on some versions of compilers
option(COMPILE_WARNING_AS_ERROR "Make all warnings into errors" OFF)
if (MSVC)
	# why can't different CIs use the same flags?
	# Travis CI      : CI=True & TRAVIS=True
	# Appveyor       : CI=True & APPVEYOR=True
	# Azure Pipelines: TF_BUILD=True
	if(COMPILE_WARNING_AS_ERROR)
		message(STATUS "Setting -Werror")
		target_compile_options(iio PRIVATE /WX)
	endif()
elseif (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	option(WITH_GCOV "Build with gcov profiling flags" OFF)
	if (WITH_GCOV)
		target_compile_options(iio PRIVATE -fprofile-arcs -ftest-coverage)

		# TODO: Use cmake_link_options() in CMake 3.13+
		SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -fprofile-arcs -ftest-coverage")
		SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}  -fprofile-arcs -ftest-coverage")
	endif()
	if(COMPILE_WARNING_AS_ERROR)
		message(STATUS "Setting -Werror")
		target_compile_options(iio PRIVATE -Werror)
	endif()
else()
	message(STATUS "Unknown compiler ${CMAKE_C_COMPILER_ID}")
	message(STATUS "GCC ${CMAKE_COMPILER_IS_GNUCC}")
endif()

IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	option(WITH_IIOD "Build the IIO Daemon" ON)
	option(WITH_LOCAL_BACKEND "Enable the local backend" ON)

	if (WITH_IIOD AND NOT WITH_LOCAL_BACKEND)
		message(SEND_ERROR "IIOD can only be enabled if the local backend is enabled")
	endif()
endif()

set(DOXYGEN_INPUT "${CMAKE_SOURCE_DIR}")
set(DOXYGEN_STRIP_FROM_PATH "${CMAKE_SOURCE_DIR}")

if (CPP_BINDINGS)
    list(APPEND LIBIIO_HEADERS bindings/cpp/iiopp.h)
    set(DOXYGEN_ENABLED_SECTIONS CPP_BINDINGS)
    set(DOXYGEN_INPUT "${DOXYGEN_INPUT} ${CMAKE_SOURCE_DIR}/bindings/cpp/")
    set(DOXYGEN_STRIP_FROM_PATH "${CMAKE_SOURCE_DIR}/bindings/cpp/")
    set(DOXYGEN_CPP_EXAMPLE_PATH "${CMAKE_SOURCE_DIR}/bindings/cpp/examples")
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

if (NOT LIBIIO_VERSION_GIT)
	set(LIBIIO_VERSION_GIT v${VERSION})
endif()

set(LIBIIO_VERSION ${VERSION}.g${LIBIIO_VERSION_GIT})
file(WRITE ${CMAKE_BINARY_DIR}/.version ${LIBIIO_VERSION})

if(WITH_LOCAL_BACKEND)
	target_sources(iio PRIVATE local.c)

	# Link with librt if present
	find_library(LIBRT_LIBRARIES rt)
	if (LIBRT_LIBRARIES)
		target_link_libraries(iio PRIVATE ${LIBRT_LIBRARIES})
	endif()

	option(WITH_LOCAL_CONFIG "Read local context attributes from /etc/libiio.ini" ON)
	if (WITH_LOCAL_CONFIG)
		target_sources(iio PRIVATE deps/libini/libini.c)
	endif()

	option(WITH_HWMON "Add compatibility with the hardware monitoring (hwmon) subsystem" ON)

	option(WITH_LOCAL_DMABUF_API "Add support for the DMABUF API" ON)
	if (WITH_LOCAL_DMABUF_API)
		target_sources(iio PRIVATE local-dmabuf.c)
	endif()

	option(WITH_LOCAL_MMAP_API "Use the mmap API provided in Analog Devices' kernel (not upstream)" ON)
	if (WITH_LOCAL_MMAP_API)
		target_sources(iio PRIVATE local-mmap.c)

		find_library(LIBATOMIC_LIBRARIES atomic)
		if (NOT LIBATOMIC_LIBRARIES)
			# Workaround for some distributions, where the libatomic.so is
			# hidden inside /usr/lib/gcc/, where CMake won't search.
			# Point CMake to the .so.1 instead.

			set(OLD_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
			set(CMAKE_FIND_LIBRARY_SUFFIXES ".so.1")
			find_library(LIBATOMIC_LIBRARIES atomic)
			set(CMAKE_FIND_LIBRARY_SUFFIXES ${OLD_LIBRARY_SUFFIXES})
		endif()

		if (LIBATOMIC_LIBRARIES)
			target_link_libraries(iio PRIVATE ${LIBATOMIC_LIBRARIES})
		else()
			target_compile_options(iio PRIVATE -pthread)
		endif()
	endif()

	list(APPEND LIBIIO_SCAN_BACKENDS local)
endif()

option(WITH_SERIAL_BACKEND "Enable the serial backend" OFF)
if (WITH_SERIAL_BACKEND)
	find_library(LIBSERIALPORT_LIBRARIES serialport)
	find_path(LIBSERIALPORT_INCLUDE_DIR libserialport.h)
	if (NOT LIBSERIALPORT_LIBRARIES OR NOT LIBSERIALPORT_INCLUDE_DIR)
		message(SEND_ERROR "Unable to find libserialport dependency.\n")
	else()
		file(STRINGS ${LIBSERIALPORT_INCLUDE_DIR}/libserialport.h LIBSERIALPORT_VERSION_STR REGEX "SP_PACKAGE_VERSION_STRING")
		string(REGEX REPLACE "#define SP_PACKAGE_VERSION_STRING \"(.*)\"" "\\1" LIBSERIALPORT_VERSION ${LIBSERIALPORT_VERSION_STR})
		if ("${LIBSERIALPORT_VERSION}" VERSION_LESS 0.1.1)
			message(SEND_ERROR "The installed version of libserialport is too old. The minimum version supported is 0.1.1.")
		endif()
	endif()

	set(IIOD_CLIENT 1)
	set(NEED_LIBXML2 1)

	if (WITH_MODULES)
		option(WITH_SERIAL_BACKEND_DYNAMIC "Build serial backend as a module instead of built-in" ON)
	else()
		set(WITH_SERIAL_BACKEND_DYNAMIC OFF)
	endif()
	if (WITH_SERIAL_BACKEND_DYNAMIC)
		add_library(iio-serial SHARED serial.c)
		target_link_libraries(iio-serial PRIVATE iio ${LIBSERIALPORT_LIBRARIES})
		target_include_directories(iio-serial PRIVATE ${LIBSERIALPORT_INCLUDE_DIR})
		set_target_properties(iio-serial PROPERTIES PREFIX lib)
	else()
		target_sources(iio PRIVATE serial.c)
		target_link_libraries(iio PRIVATE ${LIBSERIALPORT_LIBRARIES})
		target_include_directories(iio PRIVATE ${LIBSERIALPORT_INCLUDE_DIR})
	endif()
endif()

option(WITH_ZSTD "Support for ZSTD compressed metadata" ON)
if (WITH_ZSTD)
	find_library(LIBZSTD_LIBRARIES zstd)
	find_path(LIBZSTD_INCLUDE_DIR zstd.h)

	if (NOT LIBZSTD_LIBRARIES OR NOT LIBZSTD_INCLUDE_DIR)
		message(SEND_ERROR "Unable to find libzstd dependency.\n"
			"If you want to disable ZSTD compression support, set WITH_ZSTD=OFF.")
	endif()

	target_link_libraries(iio PRIVATE ${LIBZSTD_LIBRARIES})
	target_include_directories(iio PRIVATE ${LIBZSTD_INCLUDE_DIR})
endif (WITH_ZSTD)

option(WITH_NETWORK_BACKEND "Enable the network backend" ON)
if(WITH_NETWORK_BACKEND)
	if (WIN32)
		target_link_libraries(iio PRIVATE wsock32 iphlpapi ws2_32)
	endif()

	if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
		include(CheckCSourceCompiles)

		check_c_source_compiles("#include <sys/eventfd.h>\nint main(void) { return eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK); }"
			WITH_NETWORK_EVENTFD)
		if (NOT WITH_NETWORK_EVENTFD)
			check_c_source_compiles("#define _GNU_SOURCE=1\n#include <unistd.h>\n#include <fcntl.h>\nint main(void) { int fd[2]; return pipe2(fd, O_CLOEXEC | O_NONBLOCK); }"
				HAS_PIPE2)
		endif()

		if (HAS_PIPE2)
			add_definitions(-D_GNU_SOURCE=1)
		endif()
	endif()

	if (WITH_MODULES)
		option(WITH_NETWORK_BACKEND_DYNAMIC "Build network backend as a module instead of built-in" ON)
	else()
		set(WITH_NETWORK_BACKEND_DYNAMIC OFF)
	endif()

	if (WITH_NETWORK_BACKEND_DYNAMIC)
		add_library(iio-ip SHARED network.c)
		target_link_libraries(iio-ip PRIVATE iio)
		set_target_properties(iio-ip PROPERTIES PREFIX lib)
		set(IIO_NETWORK_LIB iio-ip)
	else()
		target_sources(iio PRIVATE network.c)
		set(IIO_NETWORK_LIB iio)
	endif()
	if (WIN32)
		target_sources(${IIO_NETWORK_LIB} PRIVATE network-windows.c)
	else()
		target_sources(${IIO_NETWORK_LIB} PRIVATE network-unix.c)
	endif()

	option(HAVE_DNS_SD "Enable DNS-SD (ZeroConf) support" ON)
	if (NOT HAVE_DNS_SD)
	elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
		find_library(CORE_SERVICES CoreServices)

		target_sources(${IIO_NETWORK_LIB} PRIVATE dns_sd.c dns_sd_bonjour.c)
		target_link_libraries(${IIO_NETWORK_LIB} PRIVATE ${CORE_SERVICES})
		set(HAVE_BONJOUR ON)
	elseif(WIN32)
		if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
			set_source_files_properties(dns_sd_windows.c PROPERTIES COMPILE_FLAGS "-Wno-unused-function")
		endif()

		target_sources(${IIO_NETWORK_LIB} PRIVATE dns_sd.c dns_sd_windows.c)

	else()
		find_library(AVAHI_CLIENT_LIBRARIES avahi-client)
		find_library(AVAHI_COMMON_LIBRARIES avahi-common)
		if (NOT AVAHI_CLIENT_LIBRARIES OR NOT AVAHI_COMMON_LIBRARIES)
			message(SEND_ERROR "Unable to find libavahi-common / libavahi-client dependencies.\n"
				"If you want to disable DNS-SD (ZeroConf) support, set HAVE_DNS_SD=OFF.")
		endif()

		target_sources(${IIO_NETWORK_LIB} PRIVATE dns_sd.c dns_sd_avahi.c)
		target_link_libraries(${IIO_NETWORK_LIB} PRIVATE ${AVAHI_COMMON_LIBRARIES} ${AVAHI_CLIENT_LIBRARIES})
		set(HAVE_AVAHI ON)
	endif()

	if (HAVE_DNS_SD)
		list(APPEND LIBIIO_SCAN_BACKENDS ip)
	endif()

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
	find_package(LibXml2 QUIET NO_MODULE NO_SYSTEM_ENVIRONMENT_PATH)
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

	target_sources(iio PRIVATE xml.c)

	target_include_directories(iio PRIVATE ${LIBXML2_INCLUDE_DIR})
	target_link_libraries(iio PRIVATE ${LIBXML2_LIBRARIES})
elseif(NEED_LIBXML2)
	message(SEND_ERROR "Enabled backends require the XML backend to be enabled as well.\n"
		"If you want to enable the XML backend, set WITH_XML_BACKEND=ON.")
endif()

if (WIN32)
	target_sources(iio PRIVATE lock-windows.c)
else ()
	if (NOT ANDROID)
		find_library(PTHREAD_LIBRARIES pthread)

		if (NOT PTHREAD_LIBRARIES)
			message(SEND_ERROR "Unable to find pthread dependency.")
		endif()

		target_link_libraries(iio PRIVATE ${PTHREAD_LIBRARIES})
	endif()

	target_sources(iio PRIVATE lock.c)
endif()

if (IIOD_CLIENT OR WITH_IIOD)
	add_library(iiod-responder STATIC iiod-responder.c)
	target_include_directories(iiod-responder PRIVATE include)
	set_target_properties(iiod-responder PROPERTIES POSITION_INDEPENDENT_CODE ON)

	# Link against exported symbols of Libiio and not the
	# DLL-exported symbols
	target_compile_definitions(iiod-responder PRIVATE LIBIIO_STATIC)
endif()

if (IIOD_CLIENT)
	target_sources(iio PRIVATE iiod-client.c)
	target_link_libraries(iio PRIVATE iiod-responder)
endif()

macro(toggle_iio_feature opt str)
	if (${opt})
		list(APPEND IIO_FEATURES_ON ${str})
	else()
		list(APPEND IIO_FEATURES_OFF ${str})
	endif()
endmacro()

if(WITH_UTILS)
	add_subdirectory(utils)
endif()
if(WITH_EXAMPLES)
	add_subdirectory(examples)
endif()
if (WITH_IIOD)
	if (NOT PTHREAD_LIBRARIES)
		message(SEND_ERROR "IIOD requires pthread support.\n"
			"If you want to disable IIOD, set WITH_IIOD=OFF.")
	endif()

	add_subdirectory(iiod)
endif()

if ((WITH_IIOD OR IIOD_CLIENT) AND NOT WITH_ZSTD)
	message(WARNING "IIOD async. API requires ZSTD support, consider enabling it.\n"
		"If you want to enable libzstd support, set WITH_ZSTD=ON.")
endif()

if (WIN32)
	set(LIBIIO_ORIGINAL_FILENAME libiio.dll)
	set(LIBIIO_RC ${CMAKE_CURRENT_BINARY_DIR}/properties.rc)
	configure_file(properties.rc.cmakein ${LIBIIO_RC} @ONLY)
endif()

set(CMAKE_HTML_DEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/html/v${VERSION}")

option(CPP_BINDINGS "Install C++ bindings" OFF)
option(CSHARP_BINDINGS "Install C# bindings" OFF)
option(PYTHON_BINDINGS "Install Python bindings" OFF)
add_subdirectory(bindings)

option(WITH_MAN "Generate on-line reference manuals (man pages)" OFF)
add_subdirectory(man)

include(cmake/Install.cmake)

# Add uninstall target
if(NOT TARGET uninstall)
	configure_file(
		"${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
		"${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
		IMMEDIATE @ONLY
	)

	add_custom_target(uninstall
		COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()

string(REPLACE ";" "," LIBIIO_SCAN_BACKENDS "${LIBIIO_SCAN_BACKENDS}")

configure_file(iio-config.h.cmakein ${CMAKE_CURRENT_BINARY_DIR}/iio-config.h @ONLY)

toggle_iio_feature("${WITH_XML_BACKEND}" xml)
toggle_iio_feature("${WITH_ZSTD}" zstd)
toggle_iio_feature("${WITH_NETWORK_BACKEND}" network)
toggle_iio_feature("${HAVE_DNS_SD}" dns-sd)
toggle_iio_feature("${HAVE_AVAHI}" avahi)
toggle_iio_feature("${HAVE_BONJOUR}" bonjour)
toggle_iio_feature("${ENABLE_IPV6}" ipv6)
toggle_iio_feature("${WITH_SERIAL_BACKEND}" serial)
toggle_iio_feature("${WITH_LOCAL_BACKEND}" local)
toggle_iio_feature("${WITH_LOCAL_DMABUF_API}" local-dmabuf)
toggle_iio_feature("${WITH_LOCAL_MMAP_API}" local-mmap)
toggle_iio_feature("${WITH_HWMON}" hwmon)
toggle_iio_feature("${WITH_USB_BACKEND}" usb)
toggle_iio_feature("${WITH_UTILS}" utils)
toggle_iio_feature("${WITH_EXAMPLES}" examples)
toggle_iio_feature("${WITH_IIOD}" iiod)
toggle_iio_feature("${WITH_MODULES}" modules)
toggle_iio_feature("${WITH_USB_BACKEND_DYNAMIC}" usb-dynamic)
toggle_iio_feature("${WITH_NETWORK_BACKEND_DYNAMIC}" network-dynamic)
toggle_iio_feature("${WITH_SERIAL_BACKEND_DYNAMIC}" serial-dynamic)
toggle_iio_feature("${INSTALL_UDEV_RULE}" udev-rule)
#add iiod settings
list(APPEND IIO_FEATURES_ON ${IIOD_FEATURES_ON})
list(APPEND IIO_FEATURES_OFF ${IIOD_FEATURES_OFF})
toggle_iio_feature("${WITH_DOC}" doc)
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
message(STATUS "LOG_LEVEL set to \"${LOG_LEVEL}\"")
