# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# libiio as a no-OS library, the way no-OS takes lwIP: with CONFIG_LIBIIO,
# no-OS's cmake/libraries/libiio.cmake includes this file. It builds
# libiio-iiod (libiio's iiod, this port's backend and transport) and links it
# into the project, whose main() then calls noos_iiod_run() with its device
# table (include/noos_iiod.h, PORTING.md).
#
# From no-OS's Kconfig:
#   CONFIG_LIBIIO_TRANSPORT_UART / _USB / _NETWORK   the transport (uart if none)
# IIOD_TRANSPORT (uart, usb, network) overrides the transport from the command
# line.

include(${CMAKE_CURRENT_LIST_DIR}/cmake/libiio_noos_server.cmake)

if (CONFIG_LIBIIO_TRANSPORT_USB)
    set(_libiio_transport usb)
elseif (CONFIG_LIBIIO_TRANSPORT_NETWORK)
    set(_libiio_transport network)
else ()
    set(_libiio_transport uart)
endif ()
set(IIOD_TRANSPORT
    ${_libiio_transport}
    CACHE STRING "IIOD transport: uart, usb or network")

libiio_noos_add_server(libiio-iiod TRANSPORT ${IIOD_TRANSPORT})

# The platform's glue and the project's own files include libiio's headers
# (noos_iiod.h, iio_channels.h, noos_iio_usb.h, ...), and they are compiled
# in no-os.
target_include_directories(no-os PUBLIC ${LIBIIO_NOOS_DIR}/include
                                        ${LIBIIO_DIR}/include)
target_compile_definitions(no-os PUBLIC LIBIIO_STATIC=1)

# no-OS adds the project before its libraries, so the executable exists.
if (DEFINED NO_OS_PROJECT_NAME AND TARGET ${NO_OS_PROJECT_NAME})
    # Appended, not target_link_libraries(): projects link no-os with the
    # plain signature, which CMake will not mix with a keyword one.
    set_property(
        TARGET ${NO_OS_PROJECT_NAME}
        APPEND
        PROPERTY LINK_LIBRARIES libiio-iiod)
else ()
    message(
        STATUS
            "libiio: no project executable to link; link libiio-iiod yourself")
endif ()
