# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.27)

# CAPI_DIR is the CAPI checkout: an environment variable or -DCAPI_DIR=<path>.
if (NOT DEFINED CAPI_DIR)
    if (NOT DEFINED ENV{CAPI_DIR})
        message(FATAL_ERROR "CAPI_DIR not set: export CAPI_DIR=/path/to/capi "
                            "or pass -DCAPI_DIR=...")
    endif ()
    set(CAPI_DIR "$ENV{CAPI_DIR}")
endif ()

include("${CAPI_DIR}/sdk/cmake/capi_create_application.cmake")

# Sets the toolchain of the platform PLATFORM selects, so before project(),
# and resolves module.yml's packages: the two the Alif toolchain asks for.
capi_resolve_platform()
if (NOT DEFINED ENV{ALIF_CMSIS_DFP_PATH})
    set(ENV{ALIF_CMSIS_DFP_PATH} "${ALIF_ENSEMBLE_CMSIS_DFP_DIR}")
endif ()
if (NOT DEFINED ENV{CMSIS_6_PATH})
    set(ENV{CMSIS_6_PATH} "${CMSIS_6_DIR}")
endif ()
project(libiio_iiod_sample LANGUAGES C ASM)

capi_create_application(libiio_iiod_sample SOURCES src/main.c)

# libiio's CAPI port from this tree: capi/, two levels up
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../.." "${CMAKE_BINARY_DIR}/libiio")
target_link_libraries(libiio_iiod_sample PRIVATE libiio::capi)
