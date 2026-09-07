// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Stubs for iiod symbols that tinyiiod does not implement.
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include "../iiod/ops.h"

/*
 * responder.c calls this behind a runtime "if (WITH_IIOD_V0_COMPAT)". tinyiiod
 * only speaks the binary protocol and keeps no per-device sample size cache,
 * so there is nothing to invalidate.
 */
void invalidate_sample_size_cache(const struct iio_device *dev)
{
	(void)dev;
}
