/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IIO_ADC_COMMON_API_H
#define IIO_ADC_COMMON_API_H

#include "iio_adc_hal.h"
#include "adc.h"

#define MAXIM_ADC_REVB MAX_ADC_SLOT_NUM
#define MAXIM_ADC_REVA !MAXIM_ADC_REVB

#if MAXIM_ADC_REVB

#define MAXIM_ADC_RESOLUTION_BITS 12

#ifndef IIO_ADC_CLOCK
#error "define IIO_ADC_CLOCK in cmake/boards/<board>.cmake"
#endif

#ifndef IIO_ADC_REF_VOLTAGE_MV
#define IIO_ADC_REF_VOLTAGE_MV 1250
#endif

#else /* revA */

#define MAXIM_ADC_RESOLUTION_BITS 10

#if defined(MXC_V_ADC_CTRL_CH_SEL_AIN7)
#define MAXIM_ADC_AIN_COUNT 8
#elif defined(MXC_V_ADC_CTRL_CH_SEL_AIN3)
#define MAXIM_ADC_AIN_COUNT 4
#else
#error "no MXC_V_ADC_CTRL_CH_SEL_AINn in adc_regs.h: unsupported part"
#endif

#ifndef IIO_ADC_REF_VOLTAGE_MV
#error "define IIO_ADC_REF_VOLTAGE_MV in cmake/boards/<board>.cmake"
#endif

#endif /* MAXIM_ADC_REVB */

#define MAXIM_ADC_HAS_REFERENCE MAXIM_ADC_REVB

#define MAXIM_ADC_RAW_MASK ((1u << MAXIM_ADC_RESOLUTION_BITS) - 1u)

#ifndef IIO_ADC_NUM_CHANNELS
#define IIO_ADC_NUM_CHANNELS 1
#endif

#if IIO_ADC_NUM_CHANNELS < 1 || IIO_ADC_NUM_CHANNELS > IIO_ADC_MAX_CHANNELS
#error "IIO_ADC_NUM_CHANNELS out of range 1..IIO_ADC_MAX_CHANNELS"
#endif

#if MAXIM_ADC_REVA && IIO_ADC_NUM_CHANNELS > MAXIM_ADC_AIN_COUNT
#error "IIO_ADC_NUM_CHANNELS exceeds the AIN count of this part"
#endif

#endif /* IIO_ADC_COMMON_API_H */
