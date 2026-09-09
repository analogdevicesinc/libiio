cmake_minimum_required(VERSION 3.13)
project(iiod-emu C)

add_executable(iiod-emu iiod-emu.c network.c
                        ${CMAKE_SOURCE_DIR}/iiod/xml-zstd.c)
set_target_properties(
    iiod-emu
    PROPERTIES C_STANDARD 99
               C_STANDARD_REQUIRED ON
               C_EXTENSIONS OFF)

target_link_libraries(iiod-emu LINK_PRIVATE iio iio_common_config tinyiiod)
target_include_directories(iiod-emu PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
                                            ${CMAKE_CURRENT_SOURCE_DIR})

if (WITH_ZSTD)
    target_include_directories(iiod-emu PRIVATE ${LIBZSTD_INCLUDE_DIR})
    target_link_libraries(iiod-emu LINK_PRIVATE ${LIBZSTD_LIBRARIES})
endif ()

if (WIN32)
    # Keep <Windows.h> from dragging in the legacy winsock.h, which is mutually
    # exclusive with winsock2.h. Set on the target so that include order, which
    # clang-format is free to change, cannot defeat it.
    target_compile_definitions(iiod-emu PRIVATE WIN32_LEAN_AND_MEAN)
    target_link_libraries(iiod-emu LINK_PRIVATE ws2_32)
else ()
    target_compile_definitions(iiod-emu PRIVATE _GNU_SOURCE)
endif ()

if (MSVC)
    target_sources(iiod-emu PRIVATE ../deps/wingetopt/src/getopt.c)
    target_include_directories(iiod-emu PRIVATE ../deps/wingetopt/src)
    target_compile_definitions(iiod-emu PRIVATE _CRT_SECURE_NO_WARNINGS)
endif ()

if (WITH_GCOV)
    enable_coverage(iiod-emu)
endif ()

include(GNUInstallDirs)
if (NOT SKIP_INSTALL_ALL)
    install(TARGETS iiod-emu RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif ()
