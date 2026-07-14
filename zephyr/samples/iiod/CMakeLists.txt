# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20.0)

if(eval_ad4052_ardz IN_LIST SHIELD)
  list(APPEND EXTRA_DTC_OVERLAY_FILE eval_ad4052_ardz.overlay)
endif()

if(eval_adxl362_ardz IN_LIST SHIELD)
  list(APPEND EXTRA_DTC_OVERLAY_FILE eval_adxl362_ardz.overlay)
  list(APPEND EXTRA_CONF_FILE eval_adxl362_ardz.conf)
endif()

if(pmod_acl IN_LIST SHIELD)
  list(APPEND EXTRA_DTC_OVERLAY_FILE pmod_acl.overlay)
  list(APPEND EXTRA_CONF_FILE pmod_acl.conf)
endif()

if(CONFIG_LIBIIO_IIOD_ADC_EMUL)
  list(APPEND EXTRA_DTC_OVERLAY_FILE adc-emul.overlay)
  list(APPEND EXTRA_CONF_FILE adc-emul.conf)
endif()

if(CONFIG_LIBIIO_IIOD_SENSOR_EMUL)
  list(APPEND EXTRA_DTC_OVERLAY_FILE sensor-emul.overlay)
  list(APPEND EXTRA_CONF_FILE sensor-emul.conf)
endif()

if(CONFIG_LIBIIO_IIOD_MULTI_SENSOR_EMUL)
  list(APPEND EXTRA_DTC_OVERLAY_FILE multi-sensor-emul.overlay)
  list(APPEND EXTRA_CONF_FILE sensor-emul.conf)
endif()

if(CONFIG_LIBIIO_IIOD_SENSOR_ADXL345)
  list(APPEND EXTRA_DTC_OVERLAY_FILE adxl345.overlay)
  list(APPEND EXTRA_CONF_FILE adxl345.conf)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iiod)

target_sources(app PRIVATE src/main.c)
