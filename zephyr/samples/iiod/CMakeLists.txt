# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20.0)

if(eval_ad4052_ardz IN_LIST SHIELD)
  set(EXTRA_DTC_OVERLAY_FILE eval_ad4052_ardz.overlay)
else()
  set(EXTRA_DTC_OVERLAY_FILE adc-emul.overlay)
  set(EXTRA_CONF_FILE adc-emul.conf)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iiod)

target_sources(app PRIVATE src/main.c)
