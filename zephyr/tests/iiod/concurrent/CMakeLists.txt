# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20.0)

set(LIBIIO_TESTS_EMUL ${CMAKE_CURRENT_LIST_DIR}/../../common/emul)
list(APPEND EXTRA_DTC_OVERLAY_FILE ${LIBIIO_TESTS_EMUL}/adc-emul.overlay)
list(APPEND EXTRA_CONF_FILE ${LIBIIO_TESTS_EMUL}/adc-emul.conf)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(libiio_iiod_concurrent)

target_sources(app PRIVATE src/main.c)
