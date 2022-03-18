cmake_minimum_required(VERSION 3.1.0)
project(libini LANGUAGES C VERSION 0.1)

include(GNUInstallDirs)

set(INSTALL_PKGCONFIG_DIR "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
	CACHE PATH "Installation directory for pkgconfig (.pc) files")

set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries")
if (NOT BUILD_SHARED_LIBS)
	set(INI_STATIC_CFLAGS -Dlibini_STATIC)
endif (NOT BUILD_SHARED_LIBS)

set(LIBINI_PC ${CMAKE_CURRENT_BINARY_DIR}/libini.pc)
configure_file(libini.pc.cmakein ${LIBINI_PC} @ONLY)
install(FILES ${LIBINI_PC} DESTINATION "${INSTALL_PKGCONFIG_DIR}")

set(SETUP_PY ${CMAKE_CURRENT_SOURCE_DIR}/python/setup.py)
configure_file(python/setup.py.cmakein ${SETUP_PY} @ONLY)

set(LIBINI_CFILES libini.c)
set(LIBINI_HEADERS ini.h)

if (CMAKE_COMPILER_IS_GNUCC AND NOT WIN32)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
endif()

add_library(ini ${LIBINI_CFILES} ${LIBINI_HEADERS})
set_target_properties(ini PROPERTIES
	VERSION ${PROJECT_VERSION}
	SOVERSION ${PROJECT_VERSION_MAJOR}
	PUBLIC_HEADER ${LIBINI_HEADERS}
)

target_compile_definitions(ini PUBLIC ${INI_STATIC_CFLAGS})

install(TARGETS ini
	ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
	PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
