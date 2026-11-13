# Copyright (c) 2025-2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20.0)

set(LIBIIO_TESTS_EMUL ${CMAKE_CURRENT_LIST_DIR}/../../common/emul)
list(APPEND EXTRA_DTC_OVERLAY_FILE
  ${LIBIIO_TESTS_EMUL}/adc-emul.overlay
  ${LIBIIO_TESTS_EMUL}/sensor-emul.overlay
)
list(APPEND EXTRA_CONF_FILE
  ${LIBIIO_TESTS_EMUL}/adc-emul.conf
  ${LIBIIO_TESTS_EMUL}/sensor-emul.conf
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(libiio_iiod_transport)

target_sources(app PRIVATE src/main.c)
