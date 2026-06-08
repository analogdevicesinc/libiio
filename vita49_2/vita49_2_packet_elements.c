/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Praveen Perera <praveen.perera@analog.com>
 */

 #include "vita49_2_packet_elements.h"

 int64_t convert_to_44_20(double value)
{
	return (int64_t)(value * (1 << 20));
}

double convert_from_44_20(int64_t value)
{
	return (double)(value / (1 << 20));
}

int16_t convert_to_10_6(float value)
{
	return (int16_t)(value * (1 << 6));
}

float convert_from_10_6(int16_t value)
{
	return (float)(value / (1 << 6));
}

int16_t convert_to_9_7(float value)
{
	return (int16_t)(value * (1 << 7));
}

float convert_from_9_7(int16_t value)
{
	return (float)(value / (1 << 7));
}
