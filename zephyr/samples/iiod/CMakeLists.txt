# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20.0)

# IIO_DEVICE_TYPE can be set to "adc", "sensor", or specific sensor names
# Usage:
#   west build ... -- -DIIO_DEVICE_TYPE=adc        (ADC emulation)
#   west build ... -- -DIIO_DEVICE_TYPE=sensor     (Sensor emulation - ADLTC2990)
#   west build ... -- -DIIO_DEVICE_TYPE=adxl345    (Real ADXL345 accelerometer)
#   west build ... -- -DIIO_DEVICE_TYPE=multi-sensor (ADXL345 + ADLTC2990 emul)
# Default is "adc" unless a shield is specified

if(eval_ad4052_ardz IN_LIST SHIELD)
  # Real hardware shield - use its specific overlay
  set(EXTRA_DTC_OVERLAY_FILE eval_ad4052_ardz.overlay)
elseif(NOT DEFINED IIO_DEVICE_TYPE OR IIO_DEVICE_TYPE STREQUAL "adc")
  # Default: ADC emulation
  set(EXTRA_DTC_OVERLAY_FILE adc-emul.overlay)
  set(EXTRA_CONF_FILE adc-emul.conf)
  message(STATUS "Using ADC device type (default)")
elseif(IIO_DEVICE_TYPE STREQUAL "sensor")
  # Sensor emulation (ADLTC2990)
  set(EXTRA_DTC_OVERLAY_FILE sensor-emul.overlay)
  set(EXTRA_CONF_FILE sensor-emul.conf)
  message(STATUS "Using SENSOR emulation device type (ADLTC2990)")
elseif(IIO_DEVICE_TYPE STREQUAL "adxl345")
  # Real ADXL345 accelerometer
  set(EXTRA_DTC_OVERLAY_FILE adxl345.overlay)
  set(EXTRA_CONF_FILE adxl345.conf)
  message(STATUS "Using ADXL345 accelerometer")
elseif(IIO_DEVICE_TYPE STREQUAL "multi-sensor")
  # Multi-sensor: ADXL345 (real HW) + ADLTC2990 (emulated)
  set(EXTRA_DTC_OVERLAY_FILE multi-sensor.overlay)
  set(EXTRA_CONF_FILE multi-sensor.conf)
  message(STATUS "Using multi-sensor: ADXL345 + ADLTC2990")
else()
  message(FATAL_ERROR "Invalid IIO_DEVICE_TYPE: ${IIO_DEVICE_TYPE}. Must be 'adc', 'sensor', 'adxl345', or 'multi-sensor'")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iiod)

target_sources(app PRIVATE src/main.c)
