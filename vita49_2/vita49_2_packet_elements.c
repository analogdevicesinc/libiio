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

int vita49_2_parse_cif0_payload(const struct vita49_2 *pkt, struct vita49_2_cif0_fields *cif)
{
	// Starting at 
	size_t offset = 1;

	if (!pkt || !cif)
		return -1;

	memset(cif, 0, sizeof(*cif));

	if (pkt->prologue.header.packet_type != VITA49_2_PKT_TYPE_IF_CONTEXT &&
	    pkt->prologue.header.packet_type != VITA49_2_PKT_TYPE_EXT_CONTEXT)
		return -1;

	if (pkt->payload_num_words < 1)
		return -1;

	cif->cif0 = VITA49_2_get_payload_word(pkt, 0);

	if (cif->cif0 & (1 << 31)) {
		cif->context_field_change = true;
	}
	if (cif->cif0 & (1 << 30)) {
		cif->has_reference_point_id = true;
		cif->reference_point_id = VITA49_2_get_payload_word(pkt, offset);
		offset += 1;
	}
	if (cif->cif0 & (1 << 29)) {
		cif->has_bandwidth = true;
		cif->bandwidth = VITA49_2_get_payload_double(pkt, offset);
		offset += 2;
	}
	if (cif->cif0 & (1 << 28)) {
		cif->has_if_reference_frequency = true;
		cif->if_reference_frequency = VITA49_2_get_payload_double(pkt, offset);
		offset += 2;
	}
	if (cif->cif0 & (1 << 27)) {
		cif->has_rf_reference_frequency = true;
		cif->rf_reference_frequency = VITA49_2_get_payload_double(pkt, offset);
		offset += 2;
	}
	if (cif->cif0 & (1 << 26)) {
		cif->has_rf_reference_frequency_offset = true;
		cif->rf_reference_frequency_offset = VITA49_2_get_payload_double(pkt, offset);
		offset += 2;
	}
	if (cif->cif0 & (1 << 25)) {
		cif->has_if_band_offset = true;
		cif->if_band_offset = VITA49_2_get_payload_double(pkt, offset);
		offset += 2;
	}
	if (cif->cif0 & (1 << 24)) {
		cif->has_reference_level = true;
		uint32_t val = VITA49_2_get_payload_word(pkt, offset);
		float fval;
		memcpy(&fval, &val, sizeof(float));
		cif->reference_level = fval;
		offset += 1;
	}
	if (cif->cif0 & (1 << 23)) {
		cif->has_gain = true;
		uint32_t val = VITA49_2_get_payload_word(pkt, offset);
		int16_t stage1 = val >> 16;
		int16_t stage2 = val & 0xFFFF;
		cif->gain_stage_1 = (float)stage1;
		cif->gain_stage_2 = (float)stage2;
		offset += 1;
	}
	if (cif->cif0 & (1 << 22)) {
		cif->has_over_range_count = true;
		cif->over_range_count = VITA49_2_get_payload_word(pkt, offset);
		offset += 1;
	}
	if (cif->cif0 & (1 << 21)) {
		cif->has_sample_rate = true;
		cif->sample_rate = VITA49_2_get_payload_double(pkt, offset);
		offset += 2;
	}
	if (cif->cif0 & (1 << 20)) {
		cif->has_timestamp_adjustment = true;
		uint32_t w1 = VITA49_2_get_payload_word(pkt, offset);
		uint32_t w2 = VITA49_2_get_payload_word(pkt, offset + 1);
		cif->timestamp_adjustment = ((uint64_t)w1 << 32) | w2;
		offset += 2;
	}
	if (cif->cif0 & (1 << 19)) {
		cif->has_timestamp_calibration_time = true;
		cif->timestamp_calibration_time_int = VITA49_2_get_payload_word(pkt, offset);
		uint32_t f1 = VITA49_2_get_payload_word(pkt, offset + 1);
		uint32_t f2 = VITA49_2_get_payload_word(pkt, offset + 2);
		cif->timestamp_calibration_time_frac = ((uint64_t)f1 << 32) | f2;
		offset += 3;
	}
	if (cif->cif0 & (1 << 18)) {
		cif->has_temperature = true;
		uint32_t val = VITA49_2_get_payload_word(pkt, offset);
		int16_t integer = val >> 16;
		uint16_t frac = val & 0xFFFF;
		cif->temperature = (float)integer + ((float)frac / 65536.0f);
		offset += 1;
	}
	if (cif->cif0 & (1 << 17)) {
		cif->has_device_identifier = true;
		uint32_t oui = VITA49_2_get_payload_word(pkt, offset);
		uint16_t code = VITA49_2_get_payload_word(pkt, offset + 1) >> 16;
		cif->device_identifier_oui = oui & 0xFFFFFF;
		cif->device_identifier_code = code;
		offset += 2;
	}
	if (cif->cif0 & (1 << 16)) {
		cif->has_state_and_event_indicators = true;
		cif->state_and_event_indicators = VITA49_2_get_payload_word(pkt, offset);
		offset += 1;
	}
	if (cif->cif0 & (1 << 15)) {
		cif->has_data_packet_payload_format = true;
		uint32_t w1 = VITA49_2_get_payload_word(pkt, offset);
		uint32_t w2 = VITA49_2_get_payload_word(pkt, offset + 1);
		cif->data_packet_payload_format = ((uint64_t)w1 << 32) | w2;
		offset += 2;
	}

	return 0;
}

