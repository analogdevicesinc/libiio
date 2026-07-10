/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Praveen Perera <praveen.perera@analog.com>
 */

#include "vita49_2_packet_elements.h"

#include <stddef.h>
#include <errno.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
    #include "winsock2.h"
#else
	#include <arpa/inet.h>
#endif

const size_t vita49_2_cif0_field_offsets[] = {
	0,	// Reserved Bit 0
	0,	// CIF1 Enable doesn't have an attribute field
	0,	// CIF2 Enable doesn't have an attribute field
	0,	// CIF3 Enable doesn't have an attribute field
	0,	// Reserved Bit 4
	0,	// Reserved Bit 5
	0,	// Reserved Bit 6
	0,	// CIF7 Enable doesn't have an attribute field
	offsetof(struct vita49_2_cif0_fields, context_association_lists),
	offsetof(struct vita49_2_cif0_fields, gps_ascii),
	offsetof(struct vita49_2_cif0_fields, ephemeris_ref_id),
	offsetof(struct vita49_2_cif0_fields, relative_ephemeris),
	offsetof(struct vita49_2_cif0_fields, ecef_ephemeris),
	offsetof(struct vita49_2_cif0_fields, formatted_ins),
	offsetof(struct vita49_2_cif0_fields, formatted_gps),
	offsetof(struct vita49_2_cif0_fields, data_packet_payload_format),
	offsetof(struct vita49_2_cif0_fields, state_and_event_indicators),
	offsetof(struct vita49_2_cif0_fields, device_identifier),
	offsetof(struct vita49_2_cif0_fields, temperature),
	offsetof(struct vita49_2_cif0_fields, timestamp_calibration_time_int),
	offsetof(struct vita49_2_cif0_fields, timestamp_adjustment),
	offsetof(struct vita49_2_cif0_fields, sample_rate),
	offsetof(struct vita49_2_cif0_fields, over_range_count),
	offsetof(struct vita49_2_cif0_fields, gains),
	offsetof(struct vita49_2_cif0_fields, reference_level),
	offsetof(struct vita49_2_cif0_fields, if_band_offset),
	offsetof(struct vita49_2_cif0_fields, rf_reference_frequency_offset),
	offsetof(struct vita49_2_cif0_fields, rf_reference_frequency),
	offsetof(struct vita49_2_cif0_fields, if_reference_frequency),
	offsetof(struct vita49_2_cif0_fields, bandwidth),
	offsetof(struct vita49_2_cif0_fields, reference_point_id),
	0	// Context Field Change doesn't have an attribute field since it's just an indicator
};

const size_t vita49_2_cif0_field_sizes [32] = {
	0,	// Reserved Bit 0
	0,	// CIF1 Enable doesn't have an attribute field
	0,	// CIF2 Enable doesn't have an attribute field
	0,	// CIF3 Enable doesn't have an attribute field
	0,	// Reserved Bit 4
	0,	// Reserved Bit 5
	0,	// Reserved Bit 6
	0,	// CIF7 Enable doesn't have an attribute field
	sizeof(((struct vita49_2_cif0_fields *)0)->context_association_lists),
    sizeof(((struct vita49_2_cif0_fields *)0)->gps_ascii),
    sizeof(((struct vita49_2_cif0_fields *)0)->ephemeris_ref_id),
    sizeof(((struct vita49_2_cif0_fields *)0)->relative_ephemeris),
    sizeof(((struct vita49_2_cif0_fields *)0)->ecef_ephemeris),
    sizeof(((struct vita49_2_cif0_fields *)0)->formatted_ins),
    sizeof(((struct vita49_2_cif0_fields *)0)->formatted_gps),
    sizeof(((struct vita49_2_cif0_fields *)0)->data_packet_payload_format),
    sizeof(((struct vita49_2_cif0_fields *)0)->state_and_event_indicators),
    sizeof(((struct vita49_2_cif0_fields *)0)->device_identifier),
    sizeof(((struct vita49_2_cif0_fields *)0)->temperature),
    sizeof(((struct vita49_2_cif0_fields *)0)->timestamp_calibration_time_int),
    sizeof(((struct vita49_2_cif0_fields *)0)->timestamp_adjustment),
    sizeof(((struct vita49_2_cif0_fields *)0)->sample_rate),
    sizeof(((struct vita49_2_cif0_fields *)0)->over_range_count),
    sizeof(((struct vita49_2_cif0_fields *)0)->gains),
    sizeof(((struct vita49_2_cif0_fields *)0)->reference_level),
    sizeof(((struct vita49_2_cif0_fields *)0)->if_band_offset),
    sizeof(((struct vita49_2_cif0_fields *)0)->rf_reference_frequency_offset),
    sizeof(((struct vita49_2_cif0_fields *)0)->rf_reference_frequency),
    sizeof(((struct vita49_2_cif0_fields *)0)->if_reference_frequency),
	sizeof(((struct vita49_2_cif0_fields *)0)->bandwidth),
	sizeof(((struct vita49_2_cif0_fields *)0)->reference_point_id),
	0	// Context Field Change doesn't have an attribute field since it's just an indicator
};

__vrt_api int64_t convert_to_44_20(double value)
{
	return (int64_t)(value * (1 << 20));
}

 double convert_from_44_20(int64_t value)
{
	return (double)(value) / (1 << 20);
}

__vrt_api int16_t convert_to_10_6(float value)
{
	return (int16_t)(value * (1 << 6));
}

__vrt_api float convert_from_10_6(int16_t value)
{
	return (float)(value) / (1 << 6);
}

__vrt_api int16_t convert_to_9_7(float value)
{
	return (int16_t)(value * (1 << 7));
}

__vrt_api float convert_from_9_7(int16_t value)
{
	return (float)(value) / (1 << 7);
}

__vrt_api int vita49_2_get_payload_word(const uint32_t* const payload, uint16_t payload_size, size_t offset, uint32_t* const payload_word)
{

	if (payload == NULL || offset >= payload_size || payload_word == NULL)
		return -EINVAL;

	*payload_word = ntohl(payload[offset]);

	return 0;
}

__vrt_api void vita49_2_set_payload_word(uint32_t *payload, size_t max_words, size_t offset, uint32_t val)
{
	if (!payload || offset >= max_words)
		return;

	payload[offset] = htonl(val);
}

__vrt_api double vita49_2_get_payload_double(const uint32_t* const payload, uint16_t payload_size, size_t offset)
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

__vrt_api int64_t vita49_2_get_payload_int64(const uint32_t* const payload, uint16_t payload_size, size_t offset)
{
	if (!payload || offset + 1 >= payload_size)
		return 0.0;

	uint32_t w1 = ntohl(payload[offset]);
	uint32_t w2 = ntohl(payload[offset + 1]);

	return ((int64_t)w1 << 32) | w2;
}

__vrt_api void vita49_2_set_payload_double(uint32_t *payload, size_t max_words, size_t offset, double val)
{
	if (!payload || offset + 1 >= max_words)
		return;
	uint64_t v_int;
	memcpy(&v_int, &val, sizeof(double));
	payload[offset] = htonl((uint32_t)(v_int >> 32));
	payload[offset + 1] = htonl((uint32_t)(v_int & 0xFFFFFFFF));
}

__vrt_api ssize_t vita49_2_serialize_common_prologue(const struct vita49_2_prologue* const prologue, uint32_t* const buf, size_t max_words)
{
	if (prologue == NULL || buf == NULL || max_words == 0)
		return -EINVAL;

	// The packet size field of the header must be determined before we can write it to the buffer.
	// Therefore we'll start the index at 1 since we'll be writing the header (index = 0) last
	ssize_t buffer_index = 1;

	/* Stream ID */
	if (prologue->has_stream_id) 
	{
		if (buffer_index >= max_words) 
			return -ENOBUFS;
		
		buf[buffer_index++] = htonl(prologue->stream_id);
	}
	// Stream ID is mandatory for any Context Packet
	else
	{
		return -1;
	}

	/* Class ID */
	if (prologue->header.has_class_id) 
	{
		// +1 since the Class ID is 2 words
		if (buffer_index + 1 >= max_words) 
			return -ENOBUFS;

		buf[buffer_index++] = htonl(prologue->class_id.lower_word);		
		buf[buffer_index++] = htonl(prologue->class_id.upper_word);
	}

	/* Timestamp Int */
	if (prologue->header.ts_integer_format != VITA49_2_TSI_NONE) 
	{
		if (buffer_index >= max_words) 
			return -ENOBUFS;
		
		buf[buffer_index++] = htonl(prologue->timestamp_int);
	}

	/* Timestamp Frac */
	if (prologue->header.ts_fractional_format != VITA49_2_TSF_NONE) 
	{
		// +1 since the fractional timestamp is 2 words
		if (buffer_index + 1 >= max_words) 
			return -ENOBUFS;
		
		buf[buffer_index++] = htonl((uint32_t)(prologue->timestamp_frac >> 32));
		buf[buffer_index++] = htonl((uint32_t)(prologue->timestamp_frac & 0xFFFFFFFF));
	}

	return buffer_index;
}

__vrt_api ssize_t vita49_2_parse_common_prologue(const uint32_t* const buf, size_t buf_words, struct vita49_2_prologue* const prologue, enum vita49_2_packet_type packet_type)
{
	if (buf == NULL || buf_words == 0 || prologue == NULL)
		return -EINVAL;

	ssize_t buffer_index = 0;

	// Header
	prologue->header.word = ntohl(buf[buffer_index++]);

	if (prologue->header.packet_size_words > buf_words)
		return -ENOBUFS; /* Buffer too small for packet size */

	// Validating that this is the specified packet

	if (prologue->header.packet_type != packet_type)
		return -EINVAL;

	// Determining if Stream ID is present.
	switch (packet_type)
	{
		case VITA49_2_PKT_TYPE_IF_DATA_NO_SID:
		case VITA49_2_PKT_TYPE_EXT_DATA_NO_SID:
			break;

		case VITA49_2_PKT_TYPE_IF_DATA_WITH_SID:
		case VITA49_2_PKT_TYPE_EXT_DATA_WITH_SID:
		case VITA49_2_PKT_TYPE_IF_CONTEXT:
		case VITA49_2_PKT_TYPE_EXT_CONTEXT:
		case VITA49_2_PKT_TYPE_COMMAND:
		case VITA49_2_PKT_TYPE_EXT_COMMAND:
	
			if (buffer_index >= prologue->header.packet_size_words) 
				return -EINVAL;
			
			prologue->stream_id = ntohl(buf[buffer_index++]);
			prologue->has_stream_id = true;
	}

	// Class ID
	if (prologue->header.has_class_id) 
	{
		// + 1 since Class ID is 2 words
		if ((buffer_index + 1) >= prologue->header.packet_size_words) 
			return -EINVAL;
		
		prologue->class_id.lower_word = ntohl(buf[buffer_index++]);
		prologue->class_id.upper_word = ntohl(buf[buffer_index++]);

		prologue->has_class_id = true;
	}

	// Timestamp Integer
	if (prologue->header.ts_integer_format != VITA49_2_TSI_NONE) 
	{
		if (buffer_index >= prologue->header.packet_size_words) 
			return -EINVAL;
		
		prologue->timestamp_int = ntohl(buf[buffer_index++]);
		prologue->has_timestamp_int = true;
	}

	// Timestamp Fractional
	if (prologue->header.ts_fractional_format != VITA49_2_TSF_NONE) 
	{
		// + 1 since Fractional Timestamp is 2 words
		if (buffer_index + 1 >= prologue->header.packet_size_words) 
			return -EINVAL;

		uint32_t w1 = ntohl(buf[buffer_index++]);
		uint32_t w2 = ntohl(buf[buffer_index++]);
		
		// According to Figure 5.1.4-1, the most significant word is the first word 
		prologue->timestamp_frac = ((uint64_t)w1 << 32) | w2;
		prologue->has_timestamp_frac = true;
	}

	return buffer_index;
}

__vrt_api ssize_t vita49_2_serialize_command_prologue(const struct vita49_2_command_prologue* const command_prologue, uint32_t* const buf, size_t max_words, bool is_control)
{
	if (command_prologue == NULL || buf == NULL || max_words == 0)
		return -EINVAL;
	
	if ((is_control && command_prologue->control_cam == NULL) || (!is_control && command_prologue->ack_cam == NULL))
		return -EINVAL;	
	
	ssize_t buffer_index;

	// Common Prologue
	if ((buffer_index = vita49_2_serialize_common_prologue(&command_prologue->common_prologue, buf, max_words)) < 0)
		return buffer_index;

	// CAM
	if (buffer_index >= max_words)
		return -ENOBUFS;

	if (is_control)
		buf[buffer_index++] = htonl(command_prologue->control_cam->word);	
	else
		buf[buffer_index++] = htonl(command_prologue->ack_cam->word);	
	
	// Message ID
	if (buffer_index >= max_words)
		return -ENOBUFS;

	buf[buffer_index++] = htonl(command_prologue->message_id);

	// Controllee ID/UUID
	if (is_control)
	{
		if (command_prologue->control_cam->has_controllee_id)
		{
			// Using a 128-bit UUID
			if (command_prologue->control_cam->controllee_id_format)
			{
				if (buffer_index + 3 >= max_words)
					return -ENOBUFS;

				for (uint8_t i = 0; i < 4; i++)
					buf[buffer_index++] = htonl(command_prologue->controllee_id.uuid128[i]);
			}
			// Using a 32-bit ID
			else
			{
				if (buffer_index >= max_words)
					return -ENOBUFS;

				buf[buffer_index++] = htonl(command_prologue->controllee_id.id32);
			}
		}
	}
	else
	{
		if (command_prologue->ack_cam->has_controllee_id)
		{
			// Using a 128-bit UUID
			if (command_prologue->ack_cam->controllee_id_format)
			{
				if (buffer_index + 3 >= max_words)
					return -ENOBUFS;

				for (uint8_t i = 0; i < 4; i++)
					buf[buffer_index++] = htonl(command_prologue->controllee_id.uuid128[i]);
			}
			// Using a 32-bit ID
			else
			{
				if (buffer_index >= max_words)
					return -ENOBUFS;

				buf[buffer_index++] = htonl(command_prologue->controllee_id.id32);
			}
		}
	}
	

	// Controller ID/UUID
	if (is_control)
	{
		if (command_prologue->control_cam->has_controller_id)
		{
			// Using a 128-bit UUID
			if (command_prologue->control_cam->controller_id_format)
			{
				if (buffer_index + 3 >= max_words)
					return -ENOBUFS;

				for (uint8_t i = 0; i < 4; i++)
					buf[buffer_index++] = htonl(command_prologue->controller_id.uuid128[i]);
			}
			// Using a 32-bit ID
			else
			{
				if (buffer_index >= max_words)
					return -ENOBUFS;

				buf[buffer_index++] = htonl(command_prologue->controller_id.id32);
			}
		}
	}
	else
	{
		if (command_prologue->ack_cam->has_controller_id)
		{
			// Using a 128-bit UUID
			if (command_prologue->ack_cam->controller_id_format)
			{
				if (buffer_index + 3 >= max_words)
					return -ENOBUFS;

				for (uint8_t i = 0; i < 4; i++)
					buf[buffer_index++] = htonl(command_prologue->controller_id.uuid128[i]);
			}
			// Using a 32-bit ID
			else
			{
				if (buffer_index >= max_words)
					return -ENOBUFS;

				buf[buffer_index++] = htonl(command_prologue->controller_id.id32);
			}
		}
	}
	
	return buffer_index;
}

__vrt_api ssize_t vita49_2_parse_command_prologue(const uint32_t* const buf, size_t buf_words, struct vita49_2_command_prologue* const command_prologue, enum vita49_2_packet_type packet_type)
{
	if (buf == NULL || buf_words == 0 || command_prologue == NULL)
		return -EINVAL;

	ssize_t buffer_index = 0, common_prologue;

	// Common Prologue
	if ((common_prologue = vita49_2_parse_common_prologue(buf, buf_words, &command_prologue->common_prologue, packet_type)) < 0)
		return common_prologue;

	buffer_index += common_prologue;

	// CAM
	if (buffer_index >= command_prologue->common_prologue.header.packet_size_words) 
		return -EINVAL;

	// Allocating memory for the CAM if it hasn't already been initialized
	if (command_prologue->control_cam == NULL)
	{
		command_prologue->control_cam = malloc(sizeof(*command_prologue->control_cam));
		if (command_prologue->control_cam == NULL)
			return -ENOMEM;
	}

	command_prologue->control_cam->word = ntohl(buf[buffer_index++]);

	// Message ID
	if (buffer_index >= command_prologue->common_prologue.header.packet_size_words) 
		return -EINVAL;

	command_prologue->message_id = ntohl(buf[buffer_index++]);

	// Controllee ID/UUID
	if (command_prologue->control_cam->has_controllee_id)
	{
		// If Controllee ID Format is asserted, that means a 128-bit UUID is used
		if (command_prologue->control_cam->controllee_id_format)
		{
			if (buffer_index + 3 >= command_prologue->common_prologue.header.packet_size_words)
				return -EINVAL;

			for (uint8_t i = 0; i < 4; i++)
			{
				command_prologue->controllee_id.uuid128[i] = ntohl(buf[buffer_index++]);
			}
		}
		// Otherwise a 32-bit ID is used
		else
		{
			if (buffer_index >= command_prologue->common_prologue.header.packet_size_words)
				return -EINVAL;

			command_prologue->controllee_id.id32 = ntohl(buf[buffer_index++]);
		}
	}

	// Controller ID/UUID
	if (command_prologue->control_cam->has_controller_id)
	{
		// If Controller ID Format is asserted, that means a 128-bit UUID is used
		if (command_prologue->control_cam->controller_id_format)
		{
			if (buffer_index + 3 >= command_prologue->common_prologue.header.packet_size_words)
				return -EINVAL;

			for (uint8_t i = 0; i < 4; i++)
			{
				command_prologue->controller_id.uuid128[i] = ntohl(buf[buffer_index++]);
			}
		}
		// Otherwise a 32-bit ID is used
		else
		{
			if (buffer_index >= command_prologue->common_prologue.header.packet_size_words)
				return -EINVAL;

			command_prologue->controller_id.id32 = ntohl(buf[buffer_index++]);
		}
	}

	return buffer_index;
}

__vrt_api ssize_t vita49_2_serialize_cif0_attributes(const struct vita49_2_cif0_fields* const cif0, uint32_t* const buf, size_t offset, size_t max_words, bool is_command_packet)
{
	if (cif0 == NULL || buf == NULL || max_words == NULL)
		return -EINVAL;

	size_t buffer_index = offset;

	// Iterating through each of the 32 options in CIF0 from 31 to 0.
	// Butttt, remember that "Context Field Change Indicator" (bit 31) is just an indicator
	// and lacks an actual payload. Hence we'll be going from 30 to 0.
	for (int field_index = 30; field_index >= 0; field_index--)
	{
		// Field is not present
		if (!(cif0->word.word & (1 << field_index)))
			continue;

		// Checking if there's at least 1 byte left. For fields using more than 1 byte, they will require
		// an additional check in their case-block.
		if (buffer_index >= max_words)
			return -ENOBUFS;

		// See Table 9.1-1 in the VITA 49.2 2017 document
		switch ((1 << field_index))
		{
			// Context Association Lists
			case (1 << 8):
				if (is_command_packet)
					break;
					
				// TODO: Logic for including the stream ID of the associated Context Packet
				break;

			// GPS ASCII Field
			case (1 << 9):
				if (is_command_packet)
					break;

				// TODO: Logic for including the GPS ASCII field
				break;

			// Ephemeris Reference ID
			case (1 << 10):
				if (!is_command_packet)
					buf[buffer_index++] = htonl(cif0->ephemeris_ref_id);
				break;

			// Relative Ephemeris
			case (1 << 11):
				if (is_command_packet)
					break;

				// TODO: Logic for including the Relative Ephemeris field
				break;

			// ECEF Ephemeris
			case (1 << 12):
				if (is_command_packet)
					break;
	
				// TODO: Logic for including the ECEF Ephemeris field
				break;

			// Formatted INS
			case (1 << 13):
				if (is_command_packet)
					break;

				// TODO: Logic for including the Formatted INS field
				break;

			// Formatted GPS
			case (1 << 14):
				if (is_command_packet)
					break;

				// TODO: Logic for including the Formatted GPS field
				break;

			// Signal Data Packet Payload Format
			case (1 << 15):
				if (is_command_packet)
					break;

				// TODO: Logic for including the Signal Data Packet Payload Format
				break;

			// State/Event Indicators
			case (1 << 16):
				if (!is_command_packet)
					buf[buffer_index++] = htonl(cif0->state_and_event_indicators);
	
				break;

			// Device Identifier
			case (1 << 17):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;

				buf[buffer_index++] = htonl(cif0->device_identifier.lower_word);
				buf[buffer_index++] = htonl(cif0->device_identifier.upper_word);

				break;

			// Temperature
			case (1 << 18):
				if (is_command_packet)
					break;

				// Temperature uses a 10.6 fixed-point encoding
				buf[buffer_index++] = htonl((uint32_t)(convert_to_10_6(cif0->temperature)));
				break;

			// Timestamp Calibration Time
			case (1 << 19):
				buf[buffer_index++] = htonl(cif0->timestamp_calibration_time_int);
				break;

			// Timestamp Adjustment
			case (1 << 20):
				if (is_command_packet)
					break;

				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;

				buf[buffer_index++] = htonl((uint32_t)(cif0->timestamp_adjustment >> 32));
				buf[buffer_index++] = htonl((uint32_t)(cif0->timestamp_adjustment));

				break;

			// Sample Rate
			case (1 << 21):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;
		
				// Sample Rate uses a 44.20 fixed-point encoding
				int64_t sample_rate = convert_to_44_20(cif0->sample_rate);

				buf[buffer_index++] = htonl((uint32_t)(sample_rate >> 32));
				buf[buffer_index++] = htonl((uint32_t)(sample_rate));

				break;

			// Over-Range Count
			case (1 << 22):
				if (is_command_packet)
					break;

				buf[buffer_index++] = htonl(cif0->over_range_count);
				break;

			// Gain
			case (1 << 23):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;
		
				// Gain packs 2 9.7 fixed-point integers into the same 32-bit field.
				// The upper 16 bits represent Stage 2 gain while the lower 16 bits represent Stage 1 gain.

				buf[buffer_index++] = htonl((uint32_t)((convert_to_9_7(cif0->gains.gain_stage_2) << 16) | (convert_to_9_7(cif0->gains.gain_stage_1))));
				break;

			// Reference Level
			case (1 << 24):
				if (is_command_packet)
					break;

				// Reference Level uses 9.7 fixed-point encoding
				buf[buffer_index++] = htonl((uint32_t)(convert_to_9_7(cif0->reference_level)));
				break;

			// IF Band Offset
			case (1 << 25):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;

				// IF Band Offset uses 44.20 fixed-point encoding
				int64_t if_band_offset = convert_to_44_20(cif0->if_band_offset);

				buf[buffer_index++] = htonl((uint32_t)(if_band_offset >> 32));
				buf[buffer_index++] = htonl((uint32_t)(if_band_offset));

				break;

			// RF Reference Frequency Offset
			case (1 << 26):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;

				// RF Reference Frequency Offset uses 44.20 fixed-point encoding
				int64_t rf_reference_frequency_offset = convert_to_44_20(cif0->rf_reference_frequency_offset);

				buf[buffer_index++] = htonl((uint32_t)(rf_reference_frequency_offset >> 32));
				buf[buffer_index++] = htonl((uint32_t)(rf_reference_frequency_offset));

				break;

			// RF Reference Frequency
			case (1 << 27):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;

				// RF Reference Frequency uses 44.20 fixed-point encoding
				int64_t rf_reference_frequency = convert_to_44_20(cif0->rf_reference_frequency);

				buf[buffer_index++] = htonl((uint32_t)(rf_reference_frequency >> 32));
				buf[buffer_index++] = htonl((uint32_t)(rf_reference_frequency));

				break;
			
			// IF Reference Frequency
			case (1 << 28):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;

				// IF Reference Frequency uses 44.20 fixed-point encoding
				int64_t if_reference_frequency = convert_to_44_20(cif0->if_reference_frequency);

				buf[buffer_index++] = htonl((uint32_t)(if_reference_frequency >> 32));
				buf[buffer_index++] = htonl((uint32_t)(if_reference_frequency));

				break;

			// Bandwidth
			case (1 << 29):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;

				// Bandwidth uses 44.20 fixed-point encoding
				int64_t bandwidth = convert_to_44_20(cif0->bandwidth);

				buf[buffer_index++] = htonl((uint32_t)(bandwidth >> 32));
				buf[buffer_index++] = htonl((uint32_t)(bandwidth));

				break;

			// Reference Point Identifier
			case (1 << 30):
				buf[buffer_index++] = htonl(cif0->reference_point_id);

			default:
				break;
		}
	}

	return (buffer_index - offset);
}

__vrt_api ssize_t vita49_2_parse_cif0_attributes(uint16_t payload_size, const uint32_t* const buf, size_t payload_offset, struct vita49_2_cif0_fields *cif0)
{
	if (payload_size == 0 || buf == NULL || cif0 == NULL)
		return -EINVAL;

	const uint32_t* const payload = buf + payload_offset;
	size_t offset = 0;
	
	// Reference Point Identifier
	if (cif0->word.word & (1 << 30)) 
	{
		if (offset >= payload_size)
			return -EINVAL;

		if(vita49_2_get_payload_word(payload, payload_size, offset, &cif0->reference_point_id) < 0)
		{
			return -1;
		}

		offset++;
	}
	
	// Bandwidth
	if (cif0->word.word & (1 << 29)) 
	{
		if (offset + 1 >= payload_size)
			return -EINVAL;

		// Bandwidth is encoded as a 44.20 fixed-point value
		cif0->bandwidth = convert_from_44_20(vita49_2_get_payload_int64(payload, payload_size, offset));
		offset += 2;
	}

	// IF Reference Frequency
	if (cif0->word.word & (1 << 28)) 
	{
		if (offset + 1 >= payload_size)
			return -EINVAL;

		// IF Reference Frequency is encoded as a 44.20 fixed-point value
		cif0->if_reference_frequency = convert_from_44_20(vita49_2_get_payload_int64(payload, payload_size, offset));
		offset += 2;
	}
	
	// RF Reference Frequency
	if (cif0->word.word & (1 << 27)) 
	{
		if (offset + 1 >= payload_size)
			return -EINVAL;

		// RF Reference Frequency is encoded as a 44.20 fixed-point value
		cif0->rf_reference_frequency = convert_from_44_20(vita49_2_get_payload_int64(payload, payload_size, offset));
		offset += 2;
	}
	
	// RF Reference Frequency Offset
	if (cif0->word.word & (1 << 26)) 
	{
		if (offset + 1 >= payload_size)
			return -EINVAL;

		// RF Reference Frequency Offset is encoded as a 44.20 fixed-point value
		cif0->rf_reference_frequency_offset = convert_from_44_20(vita49_2_get_payload_int64(payload, payload_size, offset));
		offset += 2;
	}
	
	// IF Band Offset
	if (cif0->word.word & (1 << 25)) 
	{
		if (offset + 1 >= payload_size)
			return -EINVAL;

		// IF Band Offset is encoded as a 44.20 fixed-point value
		cif0->if_band_offset = convert_from_44_20(vita49_2_get_payload_int64(payload, payload_size, offset));
		offset += 2;
	}
	
	// Reference Level
	if (cif0->word.word & (1 << 24)) 
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
	if (cif0->word.word & (1 << 23)) 
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
		int16_t stage1_gain = (int16_t)(both_gains);
		int16_t stage2_gain = both_gains >> 16;

		cif0->gains.gain_stage_1 = convert_from_9_7(stage1_gain);
		cif0->gains.gain_stage_2 = convert_from_9_7(stage2_gain);
		offset++;
	}
	
	// Over-Range Count
	if (cif0->word.word & (1 << 22)) 
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
	if (cif0->word.word & (1 << 21)) 
	{
		if (offset + 1 >= payload_size)
			return -EINVAL;

		// Sample Rate is encoded as a 44.20 fixed-point value
		cif0->sample_rate = convert_from_44_20(vita49_2_get_payload_int64(payload, payload_size, offset));
		offset += 2;
	}

	// Timestamp Adjustment
	if (cif0->word.word & (1 << 20)) 
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
	if (cif0->word.word & (1 << 19)) 
	{
		if (offset >= payload_size)
			return -EINVAL;

		uint32_t timestamp_calibration_time_u;
		if (vita49_2_get_payload_word(payload, payload_size, offset, &cif0->timestamp_calibration_time_int) < 0)
			return -1;

		offset++;
	}

	// Temperature
	if (cif0->word.word & (1 << 18)) 
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
	if (cif0->word.word & (1 << 17)) 
	{
		if (offset + 1 >= payload_size)
			return -EINVAL;

		// Ensuring the reserved fields are zeroed out
		memset(&cif0->device_identifier.lower_word, 0, sizeof(cif0->device_identifier.lower_word));
		memset(&cif0->device_identifier.upper_word, 0, sizeof(cif0->device_identifier.upper_word));
		
		uint32_t oui;
		if (vita49_2_get_payload_word(payload, payload_size, offset, &oui) < 0)
			return -1;

		cif0->device_identifier.oui = oui;

		uint32_t device_code;
		if (vita49_2_get_payload_word(payload, payload_size, offset + 1, &device_code) < 0)
			return -1;
		
		cif0->device_identifier.device_code = device_code;

		offset += 2;
	}

	// State/Event Indicators
	if (cif0->word.word & (1 << 16)) 
	{
		if (offset >= payload_size)
			return -EINVAL;

		if (vita49_2_get_payload_word(payload, payload_size, offset, &cif0->state_and_event_indicators) < 0)
			return -1;
	
		offset++;
	}

	// Signal Data Packet Payload Format
	if (cif0->word.word & (1 << 15)) 
	{
		// TODO: Parse this field and populate the vita49_2_data_payload_format struct in the cif0 struct
	}

	// Formatted GPS
	if (cif0->word.word & (1 << 14))
	{
		// TODO: Parse the formatted GPS data and populate the vita49_2_formatted_gps_ins struct in the cif0 struct
	}

	// Formatted INS
	if (cif0->word.word & (1 << 13))
	{
		// TODO: Parse the formatted INS data and populate the vita49_2_formatted_gps_ins struct in the cif0 struct
	}

	// ECEF Ephemeris
	if (cif0->word.word & (1 << 12))
	{
		// TODO: Parse the ECEF Ephemeris data and populate the vita49_2_ecef_relative_ephemeris struct in the cif0 struct
	}

	// Relative Ephemeris
	if (cif0->word.word & (1 << 11))
	{
		// TODO: Parse the Relative Ephemeris data and populate the vita49_2_ecef_relative_ephemeris struct in the cif0 struct
	}

	// Ephemeris Reference ID
	if (cif0->word.word & (1 << 10))
	{
		if (offset >= payload_size)
			return -EINVAL;

		if (vita49_2_get_payload_word(payload, payload_size, offset, &cif0->ephemeris_ref_id) < 0)
			return -1;

		offset++;
	}

	// GPS ASCII
	if (cif0->word.word & (1 << 9))
	{
		// TODO: Parse the GPS ASCII data and populate the vita49_2_gps_ascii struct in the cif0 struct
	}

	// Context Association Lists
	if (cif0->word.word & (1 << 8))
	{
		// TODO: Parse the Context Association Lists data and populate the vita49_2_context_association_lists struct in the cif0 struct
	}

	// Bit 7 is CIF7 Enable which is handled outside this function

	// Bits 6 through 4 are reserved

	// Bits 3 through 1 are for CIF3-1 which are handled outside this function

	// Bit 0 is reserved

	return offset;
}

__vrt_api ssize_t vita49_2_parse_cif_fields(uint16_t payload_size, const uint32_t* const buf, size_t payload_offset, struct vita49_2_cif0_fields* const cif0, struct vita49_2_cif1_fields* cif1, struct vita49_2_cif2_fields* cif2, struct vita49_2_cif3_fields* cif3, struct vita49_2_cif7_fields* cif7)
{
	if (payload_size == 0 || buf == NULL || cif0 == NULL)
		return -EINVAL;

	const uint32_t* const payload = buf + payload_offset;
	ssize_t offset = 0, ret_value;

	// CIF0 Word
	cif0->word.word = ntohl(payload[offset++]);

	// CIF1 Word
	if (cif0->word.cif1_enable)
	{
		if (offset >= payload_size) 
			return -EINVAL;

		// Checking if memory needs to be allocated for the CIF1 struct
		if (cif1 == NULL)
		{
			cif1 = malloc(sizeof(struct vita49_2_cif1_fields));
			if (cif1 == NULL)
				return -ENOMEM;
		}
		
		cif1->word.word = ntohl(payload[offset++]);
	}

	// CIF2 Word
	if (cif0->word.cif2_enable)
	{
		if (offset >= payload_size) 
			return -EINVAL;

		// Checking if memory needs to be allocated for the CIF2 struct
		if (cif2 == NULL)
		{
			cif2 = malloc(sizeof(struct vita49_2_cif2_fields));
			if (cif2 == NULL)
				return -ENOMEM;
		}
		
		cif2->word.word = ntohl(payload[offset++]);
	}
	
	// CIF3 Word
	if (cif0->word.cif3_enable)
	{
		if (offset >= payload_size) 
			return -EINVAL;

		// Checking if memory needs to be allocated for the CIF2 struct
		if (cif3 == NULL)
		{
			cif3 = malloc(sizeof(struct vita49_2_cif3_fields));
			if (cif3 == NULL)
				return -ENOMEM;
		}
		
		cif3->word.word = ntohl(payload[offset++]);
	}
	
	// CIF7 Word
	if (cif0->word.cif7_enable)
	{
		if (offset >= payload_size) 
			return -EINVAL;

		// Checking if memory needs to be allocated for the CIF2 struct
		if (cif7 == NULL)
		{
			cif7 = malloc(sizeof(struct vita49_2_cif7_fields));
			if (cif7 == NULL)
				return -ENOMEM;
		}
		
		cif7->word.word = ntohl(payload[offset++]);
	}

	// CIF0 Attributes
	if (cif0->word.word != 0)
	{
		if ((ret_value = vita49_2_parse_cif0_attributes(payload_size - offset, payload, offset, cif0)) < 0)
			return ret_value;

		offset += ret_value;
	}
	
	// CIF1 Attributes
	// TODO: Parse the CIF 1 fields and populate the CIF 1 struct

	// CIF2 Attributes
	// TODO: Parse the CIF 2 fields and populate the CIF 2 struct

	// CIF3 Attributes
	// TODO: Parse the CIF 3 fields and populate the CIF 3 struct

	// CIF7 Attributes
	// TODO: Parse the CIF 7 fields and populate the CIF 7 struct

	return offset;
}

__vrt_api ssize_t vita49_2_serialize_cif_fields(const struct vita49_2_cif0_fields* const cif0, const struct vita49_2_cif1_fields* cif1, 
	const struct vita49_2_cif2_fields* cif2, const struct vita49_2_cif3_fields* cif3, const struct vita49_2_cif7_fields* cif7, 
	uint32_t* const buf, size_t offset, size_t max_words, bool is_command_packet)
{
	if (cif0 == NULL || buf == NULL || max_words == NULL)
		return -EINVAL;

	size_t buffer_index = offset, ret_value;

	// CIF0 Word
	if (buffer_index >= max_words)
		return -ENOBUFS;

	buf[buffer_index++] = htonl(cif0->word.word);

	// CIF1 Word
	if (cif0->word.cif1_enable && cif1 != NULL)
	{
		if (buffer_index >= max_words)
			return -ENOBUFS;

		buf[buffer_index++] = htonl(cif1->word.word);
	}

	// CIF2 Word
	if (cif0->word.cif2_enable && cif2 != NULL)
	{
		if (buffer_index >= max_words)
			return -ENOBUFS;

		buf[buffer_index++] = htonl(cif2->word.word);
	}

	// CIF3 Word
	if (cif0->word.cif3_enable && cif3 != NULL)
	{
		if (buffer_index >= max_words)
			return -ENOBUFS;

		buf[buffer_index++] = htonl(cif3->word.word);
	}

	// CIF7 Word
	if (cif0->word.cif7_enable && cif7 != NULL)
	{
		if (buffer_index >= max_words)
			return -ENOBUFS;

		buf[buffer_index++] = htonl(cif7->word.word);
	}

	// CIF0 Attribute Data
	if ((ret_value = vita49_2_serialize_cif0_attributes(cif0, buf, buffer_index, max_words, is_command_packet)) < 0)
		return ret_value;

	buffer_index += ret_value;

	// TODO: Logic for serializing CIF1/2/3/7 attribute data

	return buffer_index - offset;
}