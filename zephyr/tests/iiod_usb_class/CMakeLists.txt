# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iiod_usb_class)

target_sources(app PRIVATE src/main.c)

# The test drives the class through the USB host stack's own device and
# transfer helpers, which live in private headers.
target_include_directories(app PRIVATE ${ZEPHYR_BASE}/subsys/usb/host)
