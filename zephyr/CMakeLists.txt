# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

if(CONFIG_LIBIIO)

  zephyr_library()

  zephyr_library_sources(
    ${ZEPHYR_LIBIIO_MODULE_DIR}/context.c
    ${ZEPHYR_LIBIIO_MODULE_DIR}/library.c
    ${ZEPHYR_LIBIIO_MODULE_DIR}/backend.c
    ${ZEPHYR_LIBIIO_MODULE_DIR}/device.c
    ${ZEPHYR_LIBIIO_MODULE_DIR}/channel.c
    ${ZEPHYR_LIBIIO_MODULE_DIR}/attr.c
    ${ZEPHYR_LIBIIO_MODULE_DIR}/utilities.c
    ${ZEPHYR_LIBIIO_MODULE_DIR}/lock-dummy.c
    ${ZEPHYR_LIBIIO_MODULE_DIR}/scan.c
    ${ZEPHYR_LIBIIO_MODULE_DIR}/buffer.c
    ${ZEPHYR_LIBIIO_MODULE_DIR}/stream.c
    ${ZEPHYR_LIBIIO_MODULE_DIR}/task.c
    ${ZEPHYR_LIBIIO_MODULE_DIR}/mask.c
    ${ZEPHYR_LIBIIO_MODULE_DIR}/sort.c
    ${ZEPHYR_LIBIIO_MODULE_DIR}/block.c
    ${ZEPHYR_LIBIIO_MODULE_DIR}/events.c
    ${ZEPHYR_LIBIIO_MODULE_DIR}/tinyiiod/tinyiiod.c
    ${ZEPHYR_LIBIIO_MODULE_DIR}/iiod/responder.c
    ${ZEPHYR_LIBIIO_MODULE_DIR}/iiod-responder.c
    ${ZEPHYR_LIBIIO_MODULE_DIR}/iiod/rw.c
    ${ZEPHYR_LIBIIO_MODULE_DIR}/zephyr/backend.c
  )

  zephyr_include_directories(
    ${ZEPHYR_LIBIIO_MODULE_DIR}/zephyr/include
    ${ZEPHYR_LIBIIO_MODULE_DIR}/include
    ${ZEPHYR_LIBIIO_MODULE_DIR}
  )

  zephyr_library_compile_definitions(
    LIBIIO_STATIC=1
    NO_THREADS=1
    WITH_ZSTD=0
    WITH_LIBTINYIIOD=1
    WITH_EXTERNAL_BACKEND=1
    # Only define the minimal macros actually needed for compilation
    WITH_IIOD_USB_DMABUF=0
    WITH_LOCAL_BACKEND=0
    WITH_NETWORK_BACKEND=0
    WITH_SERIAL_BACKEND=0
    WITH_USB_BACKEND=0
    WITH_XML_BACKEND=0
    WITH_NETWORK_BACKEND_DYNAMIC=0
    WITH_SERIAL_BACKEND_DYNAMIC=0
    WITH_USB_BACKEND_DYNAMIC=0
    HAVE_DNS_SD=0
    WITH_MODULES=0
    DEFAULT_LOG_LEVEL=4
    LIBIIO_VERSION_MAJOR=1
    LIBIIO_VERSION_MINOR=0
    LIBIIO_VERSION_GIT=\"v1.0\"
    HAS_STRTOK_R=1
    LIBIIO_SCAN_BACKENDS=\"\"
  )

  add_subdirectory(drivers)

endif()
