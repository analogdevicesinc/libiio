/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Praveen Perera <praveen.perera@analog.com>
 */

#include <stddef.h>
#include <errno.h>
#include <string.h>
#if defined(_WIN32) || defined(_WIN64)
    #include "winsock2.h"
#else
	#include <arpa/inet.h>
#endif

#include "vita49_2_packet_elements.h"

int64_t convert_to_44_20(double value)
{
	return (int64_t)(value * (1 << 20));
}

double convert_from_44_20(int64_t value)
{
	return (double)(value) / (1 << 20);
}

int16_t convert_to_10_6(float value)
{
	return (int16_t)(value * (1 << 6));
}

float convert_from_10_6(int16_t value)
{
	return (float)(value) / (1 << 6);
}

int16_t convert_to_9_7(float value)
{
	return (int16_t)(value * (1 << 7));
}

float convert_from_9_7(int16_t value)
{
	return (float)(value) / (1 << 7);
}

int vita49_2_get_payload_word(const uint32_t* const payload, uint16_t payload_size, size_t offset, uint32_t* const payload_word)
{

	if (payload == NULL || offset >= payload_size || payload_word == NULL)
		return -EINVAL;

	*payload_word = ntohl(payload[offset]);

	return 0;
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

ssize_t vita49_2_parse_cif0_payload(uint16_t payload_size, const uint32_t* const payload, struct vita49_2_cif0_fields *cif0)
{
	if (!cif0 || payload_size < 1)
		return -1;

	// Preserving the CIF0 word
	uint32_t cif0_word;
	memcpy(&cif0_word, &cif0->cif0_word, sizeof(cif0_word));
	memset(cif0, 0, sizeof(*cif0));
	memcpy(&cif0->cif0_word, &cif0_word, sizeof(cif0_word));

	ssize_t offset = 0;
	
	// Reference Point Identifier
	if (cif0_word & (1 << 30)) 
	{
		if (offset >= payload_size)
			return -EINVAL;

		if(vita49_2_get_payload_word(payload, payload_size, offset, cif0->reference_point_id) < 0)
		{
			return -1;
		}

		offset++;
	}
	
	// Bandwidth
	if (cif0_word & (1 << 29)) 
	{
		if (offset + 1 >= payload_size)
			return -EINVAL;

		// Bandwidth is encoded as a 44.20 fixed-point value
		cif0->bandwidth = convert_from_44_20(vita49_2_get_payload_int64(payload, payload_size, offset));
		offset += 2;
	}

	// IF Reference Frequency
	if (cif0_word & (1 << 28)) 
	{
		if (offset + 1 >= payload_size)
			return -EINVAL;

		// IF Reference Frequency is encoded as a 44.20 fixed-point value
		cif0->if_reference_frequency = convert_from_44_20(vita49_2_get_payload_int64(payload, payload_size, offset));
		offset += 2;
	}
	
	// RF Reference Frequency
	if (cif0_word & (1 << 27)) 
	{
		if (offset + 1 >= payload_size)
			return -EINVAL;

		// RF Reference Frequency is encoded as a 44.20 fixed-point value
		cif0->rf_reference_frequency = convert_from_44_20(vita49_2_get_payload_int64(payload, payload_size, offset));
		offset += 2;
	}
	
	// RF Reference Frequency Offset
	if (cif0_word & (1 << 26)) 
	{
		if (offset + 1 >= payload_size)
			return -EINVAL;

		// RF Reference Frequency Offset is encoded as a 44.20 fixed-point value
		cif0->rf_reference_frequency_offset = convert_from_44_20(vita49_2_get_payload_int64(payload, payload_size, offset));
		offset += 2;
	}
	
	// IF Band Offset
	if (cif0_word & (1 << 25)) 
	{
		if (offset + 1 >= payload_size)
			return -EINVAL;

		// IF Band Offset is encoded as a 44.20 fixed-point value
		cif0->if_band_offset = convert_from_44_20(vita49_2_get_payload_int64(payload, payload_size, offset));
		offset += 2;
	}
	
	// Reference Level
	if (cif0_word & (1 << 24)) 
	{
		if (offset >= payload_size)
			return -EINVAL;

		// Reference Level is encoded as a 9.7 fixed-point value
		uint32_t reference_level_u;
		if (vita49_2_get_payload_word(payload, payload_size, offset, &reference_level_u) < 0)
		{
			return -1;
		}

		cif0->reference_level = convert_from_9_7((int16_t)(reference_level_u));
		offset++;
	}
	
	// Gain (Stage 1 & 2)
	if (cif0_word & (1 << 23)) 
	{
		if (offset >= payload_size)
			return -EINVAL;

		// The Gain field encodes Stage 2 gain as a 9.7 fixed-point value in the upper 16 bits
		// and Stage 1 gain as a 9.7 fixed-point value in the lower 16 bits
		uint32_t both_gains;
		if (vita49_2_get_payload_word(payload, payload_size, offset, &both_gains) < 0)
		{
			return -1;
		}
		int16_t stage1_gain = both_gains >> 16;
		int16_t stage2_gain = (int16_t)(both_gains);

		cif0->gain_stage_1 = convert_from_9_7(stage1_gain);
		cif0->gain_stage_2 = convert_from_9_7(stage2_gain);
		offset++;
	}
	
	// Over-Range Count
	if (cif0_word & (1 << 22)) 
	{
		if (offset >= payload_size)
			return -EINVAL;

		uint32_t over_range_count_u;
		if (vita49_2_get_payload_word(payload, payload_size, offset, &cif0->over_range_count) < 0)
		{
			return -1;
		}
		offset++;
	}

	// Sample Rate
	if (cif0_word & (1 << 21)) 
	{
		if (offset + 1 >= payload_size)
			return -EINVAL;

		// Sample Rate is encoded as a 44.20 fixed-point value
		cif0->sample_rate = convert_from_44_20(vita49_2_get_payload_int64(payload, payload_size, offset));
		offset += 2;
	}

	// Timestamp Adjustment
	if (cif0_word & (1 << 20)) 
	{
		if (offset + 1 >= payload_size)
			return -EINVAL;

		uint32_t w1, w2; 
		if (vita49_2_get_payload_word(payload, payload_size, offset, &w1) < 0 || vita49_2_get_payload_word(payload, payload_size, offset + 1, &w2))
		{
			return -1;
		}
		cif0->timestamp_adjustment = ((uint64_t)w1 << 32) | w2;
		offset += 2;
	}

	// Timestamp Calibration Time
	if (cif0_word & (1 << 19)) 
	{
		if (offset >= payload_size)
			return -EINVAL;

		uint32_t timestamp_calibration_time_u;
		if (vita49_2_get_payload_word(payload, payload_size, offset, &cif0->timestamp_calibration_time_int) < 0)
			return -1;

		offset++;
	}

	// Temperature
	if (cif0_word & (1 << 18)) 
	{
		if (offset >= payload_size)
			return -EINVAL;

		// Device Temperature is encoded as a 10.6 fixed-point value
		uint32_t temperature_u;
		if (vita49_2_get_payload_word(payload, payload_size, offset, &temperature_u) < 0)
			return -1;

		int16_t temperature_i16;
		memcpy(&temperature_i16, &temperature_u, sizeof(temperature_i16));

		cif0->temperature = convert_from_10_6(temperature_i16);
		offset++;
	}

	// Device Identifier
	if (cif0_word & (1 << 17)) 
	{
		if (offset + 1 >= payload_size)
			return -EINVAL;

		// Ensuring the reserved fields are zeroed out
		memset(&cif0->device_identifier.lower_word, 0, sizeof(cif0->device_identifier.lower_word));
		memset(&cif0->device_identifier.upper_word, 0, sizeof(cif0->device_identifier.upper_word));
		
		uint32_t oui;
		if (vita49_2_get_payload_word(payload, payload_size, offset, &oui) < 0)
			return -1;

		cif0->device_identifier.lower_word.oui = oui;

		uint32_t device_code;
		if (vita49_2_get_payload_word(payload, payload_size, offset + 1, &device_code) < 0)
			return -1;
		
		cif0->device_identifier.upper_word.device_code = device_code;

		offset += 2;
	}

	// State/Event Indicators
	if (cif0_word & (1 << 16)) 
	{
		if (offset >= payload_size)
			return -EINVAL;

		if (vita49_2_get_payload_word(payload, payload_size, offset, &cif0->state_and_event_indicators) < 0)
			return -1;
	
		offset++;
	}

	// Signal Data Packet Payload Format
	if (cif0_word & (1 << 15)) 
	{
		// TODO: Parse this field and populate the vita49_2_data_payload_format struct in the cif0 struct
	}

	// Formatted GPS
	if (cif0_word & (1 << 14))
	{
		// TODO: Parse the formatted GPS data and populate the vita49_2_formatted_gps_ins struct in the cif0 struct
	}

	// Formatted INS
	if (cif0_word & (1 << 13))
	{
		// TODO: Parse the formatted INS data and populate the vita49_2_formatted_gps_ins struct in the cif0 struct
	}

	// ECEF Ephemeris
	if (cif0_word & (1 << 12))
	{
		// TODO: Parse the ECEF Ephemeris data and populate the vita49_2_ecef_relative_ephemeris struct in the cif0 struct
	}

	// Relative Ephemeris
	if (cif0_word & (1 << 11))
	{
		// TODO: Parse the Relative Ephemeris data and populate the vita49_2_ecef_relative_ephemeris struct in the cif0 struct
	}

	// Ephemeris Reference ID
	if (cif0_word & (1 << 10))
	{
		if (offset >= payload_size)
			return -EINVAL;

		if (vita49_2_get_payload_word(payload, payload_size, offset, &cif0->ephemeris_ref_id) < 0)
			return -1;

		offset++;
	}

	// GPS ASCII
	if (cif0_word & (1 << 9))
	{
		// TODO: Parse the GPS ASCII data and populate the vita49_2_gps_ascii struct in the cif0 struct
	}

	// Context Association Lists
	if (cif0_word & (1 << 8))
	{
		// TODO: Parse the Context Association Lists data and populate the vita49_2_context_association_lists struct in the cif0 struct
	}

	// Bit 7 is CIF7 Enable which is handled outside this function

	// Bits 6 through 4 are reserved

	// Bits 3 through 1 are for CIF3-1 which are handled outside this function

	// Bit 0 is reserved

	return offset;
}
