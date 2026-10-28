# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# libiio_noos_add_server(<name> TRANSPORT uart|usb|network)
#
# Adds <name>, an OBJECT library with libiio's iiod for a no-OS build: libiio
# core, the responder, this port's backend,
# table layer, transport and the no-OS driver bridge. Platform glue
# (a USB transport's, an on-chip ADC) is the no-OS platform's, compiled by
# no-OS. Call it after no-OS's no-os target exists; link <name> into the
# project's executable, which links no-os itself. Used by module.cmake.

include_guard(GLOBAL)

get_filename_component(LIBIIO_NOOS_DIR "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
get_filename_component(LIBIIO_DIR "${LIBIIO_NOOS_DIR}/.." REALPATH)

function (libiio_noos_add_server name)
    cmake_parse_arguments(ARG "" "TRANSPORT" "" ${ARGN})
    set(transport ${ARG_TRANSPORT})
    if (NOT EXISTS ${LIBIIO_NOOS_DIR}/iiod/${transport}.c)
        message(
            FATAL_ERROR
                "libiio_noos_add_server: unknown TRANSPORT '${transport}' "
                "(uart, usb or network).")
    endif ()
    string(TOUPPER ${transport} _transport_upper)

    if (NOT TARGET no-os OR NOT PLATFORM)
        message(FATAL_ERROR "${name} is added from a no-OS build: the no-os "
                            "target and PLATFORM must already exist.")
    endif ()

    # libiio's responder is the only IIO server: no-OS's own (iio.c, iiod.c,
    # iio_app) comes with CONFIG_IIO, which must stay off.
    # (no-OS adds libraries before iio/, so its sources are not listed yet.)
    if (CONFIG_IIO)
        message(
            FATAL_ERROR "CONFIG_IIO and CONFIG_LIBIIO are both on: turn "
                        "CONFIG_IIO off, libiio serves IIO for the project.")
    endif ()

    # ---------- libiio core ----------
    set(LIBIIO_SRCS
        ${LIBIIO_DIR}/context.c
        ${LIBIIO_DIR}/library.c
        ${LIBIIO_DIR}/backend.c
        ${LIBIIO_DIR}/device.c
        ${LIBIIO_DIR}/channel.c
        ${LIBIIO_DIR}/attr.c
        ${LIBIIO_DIR}/utilities.c
        ${LIBIIO_DIR}/lock-dummy.c
        ${LIBIIO_DIR}/scan.c
        ${LIBIIO_DIR}/buffer.c
        ${LIBIIO_DIR}/stream.c
        ${LIBIIO_DIR}/task.c
        ${LIBIIO_DIR}/mask.c
        ${LIBIIO_DIR}/sort.c
        ${LIBIIO_DIR}/block.c
        ${LIBIIO_DIR}/events.c
        ${LIBIIO_DIR}/tinyiiod/tinyiiod.c
        ${LIBIIO_DIR}/iiod/responder.c
        ${LIBIIO_DIR}/iiod-responder.c
        ${LIBIIO_DIR}/iiod/rw.c)

    # The responder's v0 (ASCII) hooks, as stubs: libiio v1 clients only.
    list(APPEND LIBIIO_SRCS ${LIBIIO_DIR}/tinyiiod/ops-stubs.c)

    # ---------- Existing no-OS IIO drivers (iio_<part>.c), served through libiio ----------
    set(IIOD_NOOS_DRIVER_SRCS ${LIBIIO_NOOS_DIR}/drivers/iio_noos_driver_desc.c
                              ${LIBIIO_NOOS_DIR}/drivers/iio_noos_driver.c)
    # OBJECT, like no-os: its objects go straight into the project's
    # executable, and linking it to no-os carries only no-os's compile settings.
    add_library(
        ${name} OBJECT
        ${LIBIIO_SRCS} ${IIOD_NOOS_DRIVER_SRCS} ${LIBIIO_NOOS_DIR}/backend.c
        ${LIBIIO_NOOS_DIR}/backend_table.c
        ${LIBIIO_NOOS_DIR}/iiod/${transport}.c)

    # What the project's own files see: noos_iiod.h, iio_noos_driver.h and the
    # device classes' headers, none of which include libiio's iio.h. The
    # bridge's iio_noos_driver_desc.h stays private to the port.
    target_include_directories(${name} PUBLIC ${LIBIIO_NOOS_DIR}/include)
    target_include_directories(
        ${name} PRIVATE ${LIBIIO_NOOS_DIR}/drivers ${LIBIIO_NOOS_DIR}/iiod
                        ${LIBIIO_DIR}/include ${LIBIIO_DIR} ${LIBIIO_DIR}/iiod)

    target_compile_definitions(${name}
                               PUBLIC IIOD_TRANSPORT_${_transport_upper}=1)
    target_compile_definitions(
        ${name}
        PRIVATE LIBIIO_STATIC=1
                NO_THREADS=1
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
                HAVE_DNS_SD=0
                WITH_EMU_BACKEND=0
                WITH_EMU_BACKEND_DYNAMIC=0
                WITH_MODULES=0
                DEFAULT_LOG_LEVEL=0
                LIBIIO_VERSION_MAJOR=1
                LIBIIO_VERSION_MINOR=0
                LIBIIO_VERSION_PATCH=0
                LIBIIO_VERSION_GIT="v1.0"
                HAS_STRTOK_R=1
                LIBIIO_SCAN_BACKENDS=""
                DISABLE_SECURE_SOCKET=1)

    # no-os is an OBJECT library so that its strong IRQ handlers override the
    # startup file's weak ones; its objects reach only targets linking it
    # directly, so the project's executable links no-os itself, as it already
    # does in every no-OS project.
    target_link_libraries(${name} PUBLIC no-os)
endfunction ()
