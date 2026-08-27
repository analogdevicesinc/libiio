# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(libiio_lock)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
