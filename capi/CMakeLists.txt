# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

# libiio for CAPI applications.
# This directory is the CAPI package libiio (module.yml). A CAPI application
# lists it in its own module.yml (libiio: {git: ..., subdir: capi}), which
# gives it as LIBIIO_DIR, then adds it and links libiio::capi:
#
#   add_subdirectory(${LIBIIO_DIR} ${CMAKE_BINARY_DIR}/libiio)
#   target_link_libraries(<app> PRIVATE libiio::capi)

# capi::core comes from capi_create_application(), which adds CAPI's core.
if (NOT TARGET capi::core)
    message(FATAL_ERROR "libiio/capi: add it after capi_create_application(), "
                        "which provides capi::core.")
endif ()

get_filename_component(LIBIIO_CAPI_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# libiio's version, from its own top-level project(), and its git hash, as
# the top-level CMakeLists.txt sets them (this directory is not built from
# there).
file(READ "${LIBIIO_CAPI_ROOT}/CMakeLists.txt" _libiio_top)
string(REGEX MATCH "project\\([^)]*VERSION ([0-9]+)\\.([0-9]+)\\.([0-9]+)"
             _libiio_version "${_libiio_top}")
if (NOT _libiio_version)
    message(
        FATAL_ERROR
            "libiio version not found in ${LIBIIO_CAPI_ROOT}/CMakeLists.txt")
endif ()
set(LIBIIO_CAPI_VERSION_MAJOR ${CMAKE_MATCH_1})
set(LIBIIO_CAPI_VERSION_MINOR ${CMAKE_MATCH_2})
set(LIBIIO_CAPI_VERSION_PATCH ${CMAKE_MATCH_3})

find_package(Git QUIET)

# The short hash of the git repository whose top is dir, empty when dir is
# not the top of one (a copy inside another project's repository).
function (libiio_capi_git_hash dir out)
    set(${out}
        ""
        PARENT_SCOPE)
    if (NOT GIT_FOUND)
        return()
    endif ()
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --show-toplevel
        WORKING_DIRECTORY ${dir}
        OUTPUT_VARIABLE _top
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    get_filename_component(_top "${_top}" REALPATH)
    get_filename_component(_dir "${dir}" REALPATH)
    if (NOT _top STREQUAL _dir)
        return()
    endif ()
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${dir}
        OUTPUT_VARIABLE _hash
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    set(${out}
        "${_hash}"
        PARENT_SCOPE)
endfunction ()

libiio_capi_git_hash(${LIBIIO_CAPI_ROOT} LIBIIO_CAPI_VERSION_GIT)
if (NOT LIBIIO_CAPI_VERSION_GIT)
    set(LIBIIO_CAPI_VERSION_GIT
        v${LIBIIO_CAPI_VERSION_MAJOR}.${LIBIIO_CAPI_VERSION_MINOR}.${LIBIIO_CAPI_VERSION_PATCH}
    )
endif ()

# CAPI_DIR is the CAPI checkout the application builds with.
if (NOT CAPI_DIR OR NOT EXISTS "${CAPI_DIR}/module.yml")
    message(FATAL_ERROR "CAPI_DIR (\"${CAPI_DIR}\") is not a CAPI checkout: "
                        "set it to the folder with CAPI's module.yml")
endif ()

# CAPI's version, from its own module.yml, and its git hash: the context's
# backend is CAPI, so they are its backend version (backend.c).
file(
    STRINGS "${CAPI_DIR}/module.yml" _capi_version
    LIMIT_COUNT 1
    REGEX "^version:[ \t]*\"?[0-9]+\\.[0-9]+\\.[0-9]+")
string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.([0-9]+)" _capi_version
             "${_capi_version}")
if (NOT _capi_version)
    message(FATAL_ERROR "capi version not found in ${CAPI_DIR}/module.yml")
endif ()
set(LIBIIO_CAPI_BACKEND_VERSION ${_capi_version})
set(LIBIIO_CAPI_BACKEND_VERSION_MAJOR ${CMAKE_MATCH_1})
set(LIBIIO_CAPI_BACKEND_VERSION_MINOR ${CMAKE_MATCH_2})
set(LIBIIO_CAPI_BACKEND_VERSION_PATCH ${CMAKE_MATCH_3})

libiio_capi_git_hash(${CAPI_DIR} LIBIIO_CAPI_BACKEND_VERSION_GIT)
if (NOT LIBIIO_CAPI_BACKEND_VERSION_GIT)
    set(LIBIIO_CAPI_BACKEND_VERSION_GIT v${LIBIIO_CAPI_BACKEND_VERSION})
endif ()

add_library(
    libiio_capi STATIC
    ${LIBIIO_CAPI_ROOT}/context.c
    ${LIBIIO_CAPI_ROOT}/library.c
    ${LIBIIO_CAPI_ROOT}/backend.c
    ${LIBIIO_CAPI_ROOT}/device.c
    ${LIBIIO_CAPI_ROOT}/channel.c
    ${LIBIIO_CAPI_ROOT}/attr.c
    ${LIBIIO_CAPI_ROOT}/utilities.c
    ${LIBIIO_CAPI_ROOT}/scan.c
    ${LIBIIO_CAPI_ROOT}/buffer.c
    ${LIBIIO_CAPI_ROOT}/stream.c
    ${LIBIIO_CAPI_ROOT}/task.c
    ${LIBIIO_CAPI_ROOT}/mask.c
    ${LIBIIO_CAPI_ROOT}/sort.c
    ${LIBIIO_CAPI_ROOT}/block.c
    ${LIBIIO_CAPI_ROOT}/events.c
    ${LIBIIO_CAPI_ROOT}/lock-dummy.c
    ${LIBIIO_CAPI_ROOT}/tinyiiod/tinyiiod.c
    # responder.c calls invalidate_sample_size_cache() behind a runtime
    # WITH_IIOD_V0_COMPAT: depending on the compiler and its optimisation, the
    # call stays and needs this stub
    ${LIBIIO_CAPI_ROOT}/tinyiiod/ops-stubs.c
    ${LIBIIO_CAPI_ROOT}/iiod/responder.c
    ${LIBIIO_CAPI_ROOT}/iiod-responder.c
    ${LIBIIO_CAPI_ROOT}/iiod/rw.c
    backend.c
    iiod/uart.c)

add_library(libiio::capi ALIAS libiio_capi)

# The application sees this port's headers and libiio's public ones;
# libiio's internal headers (iio-private.h, tinyiiod/ ...) stay private.
target_include_directories(libiio_capi PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include
                                              ${LIBIIO_CAPI_ROOT}/include)
target_include_directories(libiio_capi PRIVATE ${LIBIIO_CAPI_ROOT})

target_compile_definitions(
    libiio_capi
    PUBLIC LIBIIO_STATIC=1
    PRIVATE NO_THREADS=1
            WITH_ZSTD=0
            WITH_LIBTINYIIOD=1
            WITH_EXTERNAL_BACKEND=1
            WITH_IIOD_USB_DMABUF=0
            WITH_IIOD_V0_COMPAT=0
            WITH_LOCAL_BACKEND=0
            WITH_NETWORK_BACKEND=0
            WITH_SERIAL_BACKEND=0
            WITH_USB_BACKEND=0
            WITH_XML_BACKEND=0
            WITH_NETWORK_BACKEND_DYNAMIC=0
            WITH_SERIAL_BACKEND_DYNAMIC=0
            WITH_USB_BACKEND_DYNAMIC=0
            WITH_EMU_BACKEND=0
            WITH_EMU_BACKEND_DYNAMIC=0
            HAVE_DNS_SD=0
            WITH_MODULES=0
            # libiio's own logs go to stdout/stderr, which may be the IIOD
            # UART: off (iiod_uart_run() also turns the responder's off).
            # This port logs through CAPI_LOG.
            DEFAULT_LOG_LEVEL=1
            LIBIIO_VERSION_MAJOR=${LIBIIO_CAPI_VERSION_MAJOR}
            LIBIIO_VERSION_MINOR=${LIBIIO_CAPI_VERSION_MINOR}
            LIBIIO_VERSION_PATCH=${LIBIIO_CAPI_VERSION_PATCH}
            LIBIIO_VERSION_GIT=\"${LIBIIO_CAPI_VERSION_GIT}\"
            CAPI_BACKEND_VERSION=\"${LIBIIO_CAPI_BACKEND_VERSION}\"
            CAPI_BACKEND_VERSION_MAJOR=${LIBIIO_CAPI_BACKEND_VERSION_MAJOR}
            CAPI_BACKEND_VERSION_MINOR=${LIBIIO_CAPI_BACKEND_VERSION_MINOR}
            CAPI_BACKEND_VERSION_PATCH=${LIBIIO_CAPI_BACKEND_VERSION_PATCH}
            CAPI_BACKEND_VERSION_GIT=\"${LIBIIO_CAPI_BACKEND_VERSION_GIT}\"
            HAS_STRTOK_R=1
            LIBIIO_SCAN_BACKENDS=\"\")

target_link_libraries(libiio_capi PUBLIC capi::core)
