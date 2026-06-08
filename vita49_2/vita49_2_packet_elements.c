/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Praveen Perera <praveen.perera@analog.com>
 */

#include <stddef.h>
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

uint32_t vita49_2_get_payload_word(const uint32_t* const payload, uint16_t payload_size, size_t offset)
{
	if (!payload || offset >= payload_size)
		return 0;

	return ntohl(payload[offset]);
}

void vita49_2_set_payload_word(uint32_t *payload, size_t max_words, size_t offset, uint32_t val)
{
	if (!payload || offset >= max_words)
		return;
	payload[offset] = htonl(val);
}

double vita49_2_get_payload_double(const uint32_t* const payload, uint16_t payload_size, size_t offset)
{
	if (!payload || offset + 1 >= payload_size)
		return 0.0;

	uint32_t w1 = ntohl(payload[offset]);
	uint32_t w2 = ntohl(payload[offset + 1]);

	uint64_t v_int = ((uint64_t)w1 << 32) | w2;
	
	double val;
	memcpy(&val, &v_int, sizeof(double));
	
	return val;
}

int64_t vita49_2_get_payload_int64(const uint32_t* const payload, uint16_t payload_size, size_t offset)
{
	if (!payload || offset + 1 >= payload_size)
		return 0.0;

	uint32_t w1 = ntohl(payload[offset]);
	uint32_t w2 = ntohl(payload[offset + 1]);

	return ((int64_t)w1 << 32) | w2;
}

void vita49_2_set_payload_double(uint32_t *payload, size_t max_words, size_t offset, double val)
{
	if (!payload || offset + 1 >= max_words)
		return;
	uint64_t v_int;
	memcpy(&v_int, &val, sizeof(double));
	payload[offset] = htonl((uint32_t)(v_int >> 32));
	payload[offset + 1] = htonl((uint32_t)(v_int & 0xFFFFFFFF));
}

int vita49_2_parse_cif0_payload(uint16_t payload_size, const uint32_t* const payload, struct vita49_2_cif0_fields *cif0)
{
	if (!cif0)
		return -1;

	memset(cif0, 0, sizeof(*cif0));

	if (payload_size < 1)
		return -1;

	size_t offset = 0;

	// Extract the CIF0 word first
	uint32_t cif0_word = vita49_2_get_payload_word(payload, payload_size, offset);
	memcpy(&cif0->cif0_word, &cif0_word, sizeof(cif0_word));
	offset++;

	// Reference Point Identifier
	if (cif0_word & (1 << 30)) 
	{
		cif0->reference_point_id = vita49_2_get_payload_word(payload, payload_size, offset);
		offset++;
	}
	
	// Bandwidth
	if (cif0_word & (1 << 29)) 
	{
		// Bandwidth is encoded as a 44.20 fixed-point value
		cif0->bandwidth = convert_from_44_20(vita49_2_get_payload_int64(payload, payload_size, offset));
		offset += 2;
	}

	// IF Reference Frequency
	if (cif0_word & (1 << 28)) 
	{
		// IF Reference Frequency is encoded as a 44.20 fixed-point value
		cif0->if_reference_frequency = convert_from_44_20(vita49_2_get_payload_int64(payload, payload_size, offset));
		offset += 2;
	}
	
	// RF Reference Frequency
	if (cif0_word & (1 << 27)) 
	{
		// RF Reference Frequency is encoded as a 44.20 fixed-point value
		cif0->rf_reference_frequency = convert_from_44_20(vita49_2_get_payload_int64(payload, payload_size, offset));
		offset += 2;
	}
	
	// RF Reference Frequency Offset
	if (cif0_word & (1 << 26)) 
	{
		// RF Reference Frequency Offset is encoded as a 44.20 fixed-point value
		cif0->rf_reference_frequency_offset = convert_from_44_20(vita49_2_get_payload_int64(payload, payload_size, offset));
		offset += 2;
	}
	
	// IF Band Offset
	if (cif0_word & (1 << 25)) 
	{
		// IF Band Offset is encoded as a 44.20 fixed-point value
		cif0->if_band_offset = convert_from_44_20(vita49_2_get_payload_int64(payload, payload_size, offset));
		offset += 2;
	}
	
	// Reference Level
	if (cif0_word & (1 << 24)) 
	{
		// Reference Level is encoded as a 9.7 fixed-point value
		float fval = convert_from_9_7((int16_t)(vita49_2_get_payload_word(payload, payload_size, offset)));
		
		cif0->reference_level = fval;
		offset++;
	}
	
	// Gain (Stage 1 & 2)
	if (cif0_word & (1 << 23)) 
	{
		// The Gain field encodes Stage 2 gain as a 9.7 fixed-point value in the upper 16 bits
		// and Stage 1 gain as a 9.7 fixed-point value in the lower 16 bits
		uint32_t both_gains = vita49_2_get_payload_word(payload, payload_size, offset);

		int16_t stage1_gain = both_gains >> 16;
		int16_t stage2_gain = (int16_t)(both_gains);

		cif0->gain_stage_1 = convert_from_9_7(stage1_gain);
		cif0->gain_stage_2 = convert_from_9_7(stage2_gain);
		offset++;
	}
	
	// Over-Range Count
	if (cif0_word & (1 << 22)) 
	{
		cif0->over_range_count = vita49_2_get_payload_word(payload, payload_size, offset);
		offset++;
	}

	// Sample Rate
	if (cif0_word & (1 << 21)) 
	{
		// Sample Rate is encoded as a 44.20 fixed-point value
		cif0->sample_rate = convert_from_44_20(vita49_2_get_payload_int64(payload, payload_size, offset));
		offset += 2;
	}

	// Timestamp Adjustment
	if (cif0_word & (1 << 20)) 
	{
		uint32_t w1 = vita49_2_get_payload_word(payload, payload_size, offset);
		uint32_t w2 = vita49_2_get_payload_word(payload, payload_size, offset + 1);
		cif0->timestamp_adjustment = ((uint64_t)w1 << 32) | w2;
		offset += 2;
	}

	// Timestamp Calibration Time
	if (cif0_word & (1 << 19)) 
	{
		cif0->timestamp_calibration_time_int  = vita49_2_get_payload_word(payload, payload_size, offset);
		offset++;
	}

	// Temperature
	if (cif0_word & (1 << 18)) 
	{
		// Device Temperature is encoded as a 10.6 fixed-point value
		cif0->temperature = convert_from_10_6((int16_t)(vita49_2_get_payload_word(payload, payload_size, offset)));
		offset++;
	}

	// Device Identifier
	if (cif0_word & (1 << 17)) 
	{
		// Ensuring the reserved fields are zeroed out
		memset(&cif0->device_identifier.lower_word, 0, sizeof(cif0->device_identifier.lower_word));
		memset(&cif0->device_identifier.upper_word, 0, sizeof(cif0->device_identifier.upper_word));
		
		cif0->device_identifier.lower_word.oui = vita49_2_get_payload_word(payload, payload_size, offset);
		cif0->device_identifier.upper_word.device_code = vita49_2_get_payload_word(payload, payload_size, offset + 1);
		offset += 2;
	}

	// State/Event Indicators
	if (cif0_word & (1 << 16)) 
	{
		cif0->state_and_event_indicators = vita49_2_get_payload_word(payload, payload_size, offset);
		offset++;
	}

	// Signal Data Packet Payload Format
	if (cif0_word & (1 << 15)) 
	{
		// TODO: Parse this field and populate the vita49_2_data_payload_format struct in the cif0 struct
	}

	return 0;
}
