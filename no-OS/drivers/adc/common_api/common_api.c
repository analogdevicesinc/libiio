/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdint.h>
#include <no_os_util.h>
#include "common_api.h"

#define ADC_POLL_TIMEOUT 1000000

static const char *const maxim_adc_channels[IIO_ADC_NUM_CHANNELS] = {
	"voltage0",
#if IIO_ADC_NUM_CHANNELS > 1
	"voltage1",
#endif
#if IIO_ADC_NUM_CHANNELS > 2
	"voltage2",
#endif
#if IIO_ADC_NUM_CHANNELS > 3
	"voltage3",
#endif
#if IIO_ADC_NUM_CHANNELS > 4
	"voltage4",
#endif
#if IIO_ADC_NUM_CHANNELS > 5
	"voltage5",
#endif
#if IIO_ADC_NUM_CHANNELS > 6
	"voltage6",
#endif
#if IIO_ADC_NUM_CHANNELS > 7
	"voltage7",
#endif
};

#if MAXIM_ADC_REVB

static int maxim_adc_init(void)
{
	mxc_adc_req_t adc_cfg = {
		.clock = IIO_ADC_CLOCK,
		.clkdiv = MXC_ADC_CLKDIV_4,
		.cal = MXC_ADC_SKIP_CAL,
		.ref = MXC_ADC_REF_INT_1V25,
		.trackCount = 4,
		.idleCount = 17,
	};

	return MXC_ADC_Init(&adc_cfg);
}

static int maxim_adc_read_raw(unsigned int channel, int *value)
{
	mxc_adc_slot_req_t slot_req = { 0 };
	mxc_adc_conversion_req_t conv_req = {
		.mode = MXC_ADC_ATOMIC_CONV,
		.trig = MXC_ADC_TRIG_SOFTWARE,
		.fifo_format = MXC_ADC_DATA,
		.fifo_threshold = 0,
		.avg_number = MXC_ADC_AVG_1,
		.num_slots = 1,
	};
	int fifo[MAX_ADC_FIFO_LEN];
	volatile uint32_t timeout;
	int ret;

	if (channel >= NO_OS_ARRAY_SIZE(maxim_adc_channels))
		return -EINVAL;

	slot_req.channel = MXC_ADC_CH_0 + channel;

	MXC_ADC_Clear_ChannelSelect();

	ret = MXC_ADC_SlotConfiguration(&slot_req, 0);
	if (ret)
		goto out;

	ret = MXC_ADC_Configuration(&conv_req);
	if (ret)
		goto out;

	ret = MXC_ADC_StartConversion();
	if (ret)
		goto out;

	timeout = ADC_POLL_TIMEOUT;
	while (!(MXC_ADC_GetFlags() & MXC_F_ADC_INTFL_SEQ_DONE)) {
		if (--timeout == 0) {
			ret = -ETIMEDOUT;
			goto out_disable;
		}
	}

	MXC_ADC_ClearFlags(MXC_F_ADC_INTFL_SEQ_DONE);
	MXC_ADC_DisableConversion();

	/*
	 * MXC_ADC_GetData() copies every word the FIFO holds, and it can hold
	 * more than the one conversion asked for: give it room for all of them
	 * and keep the first, or the extra words land past the caller's int.
	 * The conversion is stopped first so the level cannot grow meanwhile.
	 */
	if (MXC_ADC_FIFO_Level() > MAX_ADC_FIFO_LEN)
		return -EIO;

	ret = MXC_ADC_GetData(fifo);
	if (ret <= 0)
		return ret ? ret : -EIO;

	*value = fifo[0] & MAXIM_ADC_RAW_MASK;

	return 0;

out_disable:
	MXC_ADC_DisableConversion();
out:
	return ret;
}

static int maxim_adc_set_reference(unsigned int channel, unsigned int reference)
{
	mxc_adc_refsel_t ref;

	if (channel >= NO_OS_ARRAY_SIZE(maxim_adc_channels))
		return -EINVAL;

	switch (reference) {
	case IIO_ADC_REF_INTERNAL:
		ref = MXC_ADC_REF_INT_1V25;
		break;
	case IIO_ADC_REF_EXTERNAL0:
		ref = MXC_ADC_REF_EXT;
		break;
	default:
		return -ENOTSUP;
	}

	if (MXC_ADC_ReferenceSelect(ref))
		return -EINVAL;

	return 0;
}

#else /* revA */

static int maxim_adc_init(void)
{
	int ret;

	ret = MXC_ADC_Init();
	if (ret)
		return ret;

	MXC_ADC_SetDataAlignment(0);

	return 0;
}

static int maxim_adc_read_raw(unsigned int channel, int *value)
{
	int raw;

	if (channel >= NO_OS_ARRAY_SIZE(maxim_adc_channels))
		return -EINVAL;

	raw = MXC_ADC_StartConversion(MXC_ADC_CH_0 + channel);
	if (raw < 0)
		return raw;

	*value = raw & MAXIM_ADC_RAW_MASK;

	return 0;
}

#endif /* MAXIM_ADC_REVB */

const struct iio_adc_hal iio_adc_hal = {
	.channels        = maxim_adc_channels,
	.num_channels    = NO_OS_ARRAY_SIZE(maxim_adc_channels),
	.resolution_bits = MAXIM_ADC_RESOLUTION_BITS,
	.ref_voltage_mv  = IIO_ADC_REF_VOLTAGE_MV,
	.init            = maxim_adc_init,
	.read_raw        = maxim_adc_read_raw,
#if MAXIM_ADC_HAS_REFERENCE
	.set_reference   = maxim_adc_set_reference,
#endif
};
