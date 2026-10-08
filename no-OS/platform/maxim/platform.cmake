# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# Maxim glue for the iiod firmware. The no-os library already carries the
# MSDK peripheral drivers, startup code and linker script for TARGET; this
# file only adds what the libiio port needs on top of it:
#
#   IIOD_PLATFORM_SRCS      MSDK sources the no-os target list leaves out
#   IIOD_PLATFORM_INCLUDES  their private header dirs
#   IIOD_PLATFORM_DEFS      part-specific defines (ADC wiring)
#   IIOD_PLATFORM_ADC       default ADC HAL under drivers/adc/
#
# Read after add_subdirectory(no-OS), so the no-os target and MAXIM_LIBRARIES
# (set by the no-OS toolchain file) are both known.

set(IIOD_PLATFORM_SRCS "")
set(IIOD_PLATFORM_INCLUDES "")
set(IIOD_PLATFORM_DEFS "")
set(IIOD_PLATFORM_ADC adc_demo)

set(_msdk "${MAXIM_LIBRARIES}/PeriphDrivers")
get_target_property(_no_os_srcs no-os SOURCES)

# Adds an MSDK source unless the no-OS target file for TARGET compiles it
# already: a second copy would be a duplicate symbol at link time.
function(_maxim_add_msdk subdir)
  foreach(_file ${ARGN})
    set(_src "${_msdk}/Source/${subdir}/${_file}.c")
    if(NOT EXISTS "${_src}")
      message(FATAL_ERROR "${TARGET}: MSDK source ${subdir}/${_file}.c not found under ${_msdk}")
    endif()
    list(FIND _no_os_srcs "${_src}" _idx)
    if(_idx EQUAL -1)
      list(APPEND IIOD_PLATFORM_SRCS "${_src}")
    endif()
  endforeach()
  list(APPEND IIOD_PLATFORM_INCLUDES "${_msdk}/Source/${subdir}" "${_msdk}/Include/${subdir}")
  set(IIOD_PLATFORM_SRCS "${IIOD_PLATFORM_SRCS}" PARENT_SCOPE)
  set(IIOD_PLATFORM_INCLUDES "${IIOD_PLATFORM_INCLUDES}" PARENT_SCOPE)
endfunction()

# ---------- Stack: the MSDK startup file defaults to 4 KiB ----------
# The responder needs far more, and a 4 KiB stack overflows into the heap.
# The startup file is built inside the no-os target, so the define goes
# there, as no-OS does itself for aducm3029.
set(IIOD_STACK_SIZE 0x10000 CACHE STRING "Main stack size (bytes)")
target_compile_definitions(no-os PRIVATE __STACK_SIZE=${IIOD_STACK_SIZE})

# ---------- ADC: parts the common_api HAL has been brought up on ----------
if(TARGET_NUM STREQUAL "32690")
  _maxim_add_msdk(ADC adc_me18 adc_revb)
  list(APPEND IIOD_PLATFORM_DEFS IIO_ADC_CLOCK=MXC_ADC_CLK_IBRO)
  set(IIOD_PLATFORM_ADC common_api)
elseif(TARGET_NUM STREQUAL "32655")
  _maxim_add_msdk(ADC adc_me17 adc_reva)
  list(APPEND IIOD_PLATFORM_DEFS IIO_ADC_REF_VOLTAGE_MV=1220)
  set(IIOD_PLATFORM_ADC common_api)
endif()

# ---------- USB: only the parts with the USB-HS core ----------
if(IIOD_TRANSPORT STREQUAL "usb")
  if(NOT EXISTS "${NO_OS_PATH}/drivers/platform/maxim/${TARGET}/maxim_usb_uart.c")
    message(FATAL_ERROR
      "${TARGET} has no USB-HS device core in no-OS "
      "(no drivers/platform/maxim/${TARGET}/maxim_usb_uart.c). "
      "Pick the uart transport for this board.")
  endif()
  list(APPEND IIOD_PLATFORM_SRCS "${CMAKE_CURRENT_LIST_DIR}/iio_usb_backend.c")
endif()

# ---------- Network: the ADIN1110 is wired as on the AD-APARD32690-SL ----------
if(IIOD_TRANSPORT STREQUAL "network")
  list(APPEND IIOD_PLATFORM_DEFS NETDEV_HEADER="netdev_adin1110.h")
endif()

list(REMOVE_DUPLICATES IIOD_PLATFORM_INCLUDES)

unset(_msdk)
unset(_no_os_srcs)
