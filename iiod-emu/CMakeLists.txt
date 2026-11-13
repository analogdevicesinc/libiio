cmake_minimum_required(VERSION 3.13)
project(iiod-emu C)

add_executable(iiod-emu iiod-emu.c network.c
                        ${CMAKE_SOURCE_DIR}/iiod/xml-zstd.c)
set_target_properties(
    iiod-emu
    PROPERTIES C_STANDARD 99
               C_STANDARD_REQUIRED ON
               C_EXTENSIONS OFF)

target_link_libraries(iiod-emu LINK_PRIVATE tinyiiod iiod-responder iio
                      iio_common_config)
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

# UDP sample mode (--udp), the reference server of the daqiri: backend's UDP
# path. Linux only, where that backend and its tests run: elsewhere (Windows,
# macOS, the BSDs) iiod-emu builds as before, without --udp. Its sendmmsg()
# and clock_nanosleep() are probed with the flags iiod-emu is compiled with:
# the top-level ones (C standard, POSIX level) and _GNU_SOURCE.
set(IIOD_EMU_UDP OFF)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(CheckSymbolExists)
    set(_saved_CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
    set(_saved_CMAKE_REQUIRED_DEFINITIONS "${CMAKE_REQUIRED_DEFINITIONS}")
    list(APPEND CMAKE_REQUIRED_FLAGS "-std=c${IIO_C_STANDARD}")
    list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE
         "-D_POSIX_C_SOURCE=${IIO_POSIX_C_SOURCE}" "-D__XSI_VISIBLE=500")
    check_symbol_exists(sendmmsg "sys/socket.h" IIOD_EMU_HAS_SENDMMSG)
    check_symbol_exists(clock_nanosleep "time.h" IIOD_EMU_HAS_CLOCK_NANOSLEEP)
    set(CMAKE_REQUIRED_FLAGS "${_saved_CMAKE_REQUIRED_FLAGS}")
    set(CMAKE_REQUIRED_DEFINITIONS "${_saved_CMAKE_REQUIRED_DEFINITIONS}")
    unset(_saved_CMAKE_REQUIRED_FLAGS)
    unset(_saved_CMAKE_REQUIRED_DEFINITIONS)

    if (IIOD_EMU_HAS_SENDMMSG AND IIOD_EMU_HAS_CLOCK_NANOSLEEP)
        set(IIOD_EMU_UDP ON)
        target_sources(iiod-emu PRIVATE udp-stream.c udp-mock.c)
        target_compile_definitions(iiod-emu PRIVATE IIOD_EMU_UDP)
    else ()
        message(STATUS "iiod-emu: no sendmmsg() or clock_nanosleep(), "
                       "building it without UDP sample mode")
    endif ()
endif ()
# For the tests of UDP sample mode
set(IIOD_EMU_UDP
    ${IIOD_EMU_UDP}
    PARENT_SCOPE)

if (MSVC)
    target_sources(iiod-emu PRIVATE ../deps/wingetopt/src/getopt.c)
    target_include_directories(iiod-emu PRIVATE ../deps/wingetopt/src)
    target_compile_definitions(iiod-emu PRIVATE _CRT_SECURE_NO_WARNINGS)
endif ()

if (WITH_GCOV)
    enable_coverage(iiod-emu)
endif ()
