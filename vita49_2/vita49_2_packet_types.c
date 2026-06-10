/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Travis Collins <travis.collins@analog.com>
 * 
 * Contributors:
 * 		- Praveen Perera <praveen.perera@analog.com>
 */

#include "vita49_2_packet_types.h"
#include <errno.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif


ssize_t vita49_2_generate_data_packet(const struct vita49_2_data_packet *pkt, uint32_t *buf, size_t max_words)
{
	if (!pkt || !buf)
		return -EINVAL;

	// The packet size field of the header must be determined before we can write it to the buffer,
	// hence we'll store the header in a local variable and update the appropriate bits once the packet
	// size has been determined at the end.
	uint32_t header_word;

	// Starting the index at 1 since we'll be writing the header (index = 0) last
	size_t buffer_index = 1;

	/* Stream ID */
	bool has_stream_id = (pkt->prologue.header.packet_type != VITA49_2_PKT_TYPE_IF_DATA_NO_SID &&
					pkt->prologue.header.packet_type != VITA49_2_PKT_TYPE_EXT_DATA_NO_SID);

	if (has_stream_id) 
	{
		if (buffer_index >= max_words) 
			return -ENOBUFS;
		
		buf[buffer_index++] = htonl(pkt->prologue.stream_id);
	}

	/* Class ID */
	if (pkt->prologue.header.has_class_id) 
	{
		// +1 since the Class ID is 2 words
		if (buffer_index + 1 >= max_words) 
			return -ENOBUFS;
		
		uint32_t class_id_word;

		memcpy(&class_id_word, &pkt->prologue.class_id.lower_word, sizeof(class_id_word));
		buf[buffer_index++] = htonl(class_id_word);
		
		memcpy(&class_id_word, &pkt->prologue.class_id.upper_word, sizeof(class_id_word));
		buf[buffer_index++] = htonl(class_id_word);
	}

	/* Timestamp Int */
	if (pkt->prologue.header.ts_integer_format != VITA49_2_TSI_NONE) 
	{
		if (buffer_index >= max_words) 
			return -ENOBUFS;
		
		buf[buffer_index++] = htonl(pkt->prologue.timestamp_int);
	}

	/* Timestamp Frac */
	if (pkt->prologue.header.ts_fractional_format != VITA49_2_TSF_NONE) 
	{
		// +1 since the fractional timestamp is 2 words
		if (buffer_index + 1 >= max_words) 
			return -ENOBUFS;
		
		buf[buffer_index++] = htonl((uint32_t)(pkt->prologue.timestamp_frac >> 32));
		buf[buffer_index++] = htonl((uint32_t)(pkt->prologue.timestamp_frac & 0xFFFFFFFF));
	}

	/* Payload */
	if (pkt->payload && pkt->payload_num_words > 0) 
	{
		// Checking if we have enough space in the buffer for the payload, as well as the trailer (if applicable)
		if (buffer_index + pkt->payload_num_words > (max_words - (pkt->has_trailer ? 1 : 0))) 
			return -ENOBUFS;

		/* Assume big-endian payload words */
		memcpy(&buf[buffer_index], pkt->payload, pkt->payload_num_words * sizeof(uint32_t));
		buffer_index += pkt->payload_num_words;
	}

	/* Trailer */
	if (pkt->has_trailer) 
	{
		if (buffer_index >= max_words) 
			return -ENOBUFS;
		
		uint32_t trailer_word;
		memcpy(&trailer_word, &pkt->trailer, sizeof(uint32_t));
		buf[buffer_index++] = htonl(trailer_word);
	}

	/* Update size in header */
	struct vita49_2_header final_hdr = pkt->prologue.header;
	final_hdr.packet_size_words = buffer_index;
	memcpy(&header_word, &final_hdr, sizeof(uint32_t));
	buf[0] = htonl(header_word);

	return buffer_index;
}

int vita49_2_parse_data_packet(const uint32_t *buf, size_t words, struct vita49_2_data_packet *pkt)
{
	if (!buf || !pkt || words == 0)
		return -EINVAL;

	memset(pkt, 0, sizeof(*pkt));

	/* Header */
	uint32_t header_word = ntohl(buf[0]);
	memcpy(&pkt->prologue.header, &header_word, sizeof(pkt->prologue.header));

	if (pkt->prologue.header.packet_size_words > words)
		return -EINVAL; /* Buffer too small for packet size */

	uint16_t buffer_index = 1;

	/* Determine if Stream ID is present */
	bool has_stream_id = (pkt->prologue.header.packet_type != VITA49_2_PKT_TYPE_IF_DATA_NO_SID &&
						pkt->prologue.header.packet_type != VITA49_2_PKT_TYPE_EXT_DATA_NO_SID);

	if (has_stream_id) 
	{
		if (buffer_index >= pkt->prologue.header.packet_size_words) 
			return -EINVAL;
		
		pkt->prologue.stream_id = ntohl(buf[buffer_index++]);
		pkt->prologue.has_stream_id = true;
	}

	/* Class ID */
	if (pkt->prologue.header.has_class_id) 
	{
		// + 1 since Class ID is 2 words
		if (buffer_index + 1 >= pkt->prologue.header.packet_size_words) 
			return -EINVAL;
		
		uint32_t w1 = ntohl(buf[buffer_index++]);
		uint32_t w2 = ntohl(buf[buffer_index++]);
		
		memcpy(&pkt->prologue.class_id.lower_word, &w1, sizeof(pkt->prologue.class_id.lower_word));
		memcpy(&pkt->prologue.class_id.upper_word, &w2, sizeof(pkt->prologue.class_id.upper_word));

		pkt->prologue.has_class_id = true;
	}

	/* Timestamp Integer */
	if (pkt->prologue.header.ts_integer_format != VITA49_2_TSI_NONE) 
	{
		if (buffer_index >= pkt->prologue.header.packet_size_words) 
			return -EINVAL;
		
		pkt->prologue.timestamp_int = ntohl(buf[buffer_index++]);
		pkt->prologue.has_timestamp_int = true;
	}

	/* Timestamp Fractional */
	if (pkt->prologue.header.ts_fractional_format != VITA49_2_TSF_NONE) 
	{
		// + 1 since Fractional Timestamp is 2 words
		if (buffer_index + 1 >= pkt->prologue.header.packet_size_words) 
			return -EINVAL;

		uint32_t w1 = ntohl(buf[buffer_index++]);
		uint32_t w2 = ntohl(buf[buffer_index++]);
		
		pkt->prologue.timestamp_frac = ((uint64_t)w1 << 32) | w2;
		pkt->prologue.has_timestamp_frac = true;
	}

	/* Trailer */
	if (pkt->has_trailer) 
	{
		if (pkt->prologue.header.packet_size_words < buffer_index + 1) 
			return -EINVAL;
		
		uint32_t trailer_word = ntohl(buf[pkt->prologue.header.packet_size_words - 1]);
		memcpy(&pkt->trailer, &trailer_word, sizeof(pkt->trailer));
		pkt->has_trailer = true;
		
		// Assuming payload is in big endian, so we're skipping the network to host byte order translation
		pkt->payload = &buf[buffer_index];
		pkt->payload_num_words = pkt->prologue.header.packet_size_words - buffer_index - 1;
	} 
	else 
	{
		// Assuming payload is in big endian, so we're skipping the network to host byte order translation
		pkt->payload = &buf[buffer_index];
		pkt->payload_num_words = pkt->prologue.header.packet_size_words - buffer_index;
	}

	return 0;
}

ssize_t vita49_2_generate_context_packet(const struct vita49_2_context_packet *pkt, uint32_t *buf, size_t max_words)
{
	if (!pkt || !buf)
		return -EINVAL;

	// The packet size field of the header must be determined before we can write it to the buffer,
	// hence we'll store the header in a local variable and update the appropriate bits once the packet
	// size has been determined at the end.
	uint32_t header_word;

	// Starting the index at 1 since we'll be writing the header (index = 0) last
	size_t buffer_index = 1;

	/* Stream ID */
	if (pkt->prologue.has_stream_id) 
	{
		if (buffer_index >= max_words) 
			return -ENOBUFS;
		
		buf[buffer_index++] = htonl(pkt->prologue.stream_id);
	}
	// Stream ID is mandatory for any Context Packet
	else
	{
		return -1;
	}

	/* Class ID */
	if (pkt->prologue.header.has_class_id) 
	{
		// +1 since the Class ID is 2 words
		if (buffer_index + 1 >= max_words) 
			return -ENOBUFS;
		
		uint32_t class_id_word;

		memcpy(&class_id_word, &pkt->prologue.class_id.lower_word, sizeof(class_id_word));
		buf[buffer_index++] = htonl(class_id_word);
		
		memcpy(&class_id_word, &pkt->prologue.class_id.upper_word, sizeof(class_id_word));
		buf[buffer_index++] = htonl(class_id_word);
	}

	/* Timestamp Int */
	if (pkt->prologue.header.ts_integer_format != VITA49_2_TSI_NONE) 
	{
		if (buffer_index >= max_words) 
			return -ENOBUFS;
		
		buf[buffer_index++] = htonl(pkt->prologue.timestamp_int);
	}

	/* Timestamp Frac */
	if (pkt->prologue.header.ts_fractional_format != VITA49_2_TSF_NONE) 
	{
		// +1 since the fractional timestamp is 2 words
		if (buffer_index + 1 >= max_words) 
			return -ENOBUFS;
		
		buf[buffer_index++] = htonl((uint32_t)(pkt->prologue.timestamp_frac >> 32));
		buf[buffer_index++] = htonl((uint32_t)(pkt->prologue.timestamp_frac & 0xFFFFFFFF));
	}

	/* Payload (CIF0 and attribute values) */
	uint32_t cif0_word, field_bit;
	memcpy(&cif0_word, &pkt->cif0.cif0_word, sizeof(cif0_word));

	if (buffer_index >= max_words)
		return -ENOBUFS;

	buf[buffer_index++] = htonl(cif0_word);

	// Iterating through each of the 32 options in CIF0 from 31 to 0.
	// Butttt, remember that "Context Field Change Indicator" (bit 31) is just an indicator
	// and lacks an actual payload. Hence we'll be going from 30 to 0.
	for (uint8_t field_index = 30; field_index >= 0; field_index--)
	{
		field_bit = (1 << field_index);

		// Field is not present
		if (!(cif0_word & field_bit))
			continue;

		// Checking if there's at least 1 byte left. For fields using more than 1 byte, they will require
		// an additional check in their case-block.
		if (buffer_index >= max_words)
			return -ENOBUFS;

		// See Table 9.1-1 in the VITA 49.2 full spec document
		switch (field_bit)
		{
			// CIF 1 Enable
			case (1 << 1):
				// TODO: Logic for including the CIF 1 components into this Context Packet
				break;

			// CIF 2 Enable
			case (1 << 2):
				// TODO: Logic for including the CIF 2 components into this Context Packet
				break;
				
			// CIF 3 Enable
			case (1 << 3):
				// TODO: Logic for including the CIF 3 components into this Context Packet
				break;

			// CIF 7 Enable
			case (1 << 7):
				// TODO: Logic for including the CIF 7 components into this Context Packet
				break;

			// Context Association Lists
			case (1 << 8):
				// TODO: Logic for including the stream ID of the associated Context Packet
				break;

			// GPS ASCII Field
			case (1 << 9):
				// TODO: Logic for including the GPS ASCII field
				break;

			// Ephemeris Reference ID
			case (1 << 10):
				buf[buffer_index++] = htonl(pkt->cif0.ephemeris_ref_id);
				break;

			// Relative Ephemeris
			case (1 << 11):
				// TODO: Logic for including the Relative Ephemeris field
				break;

			// ECEF Ephemeris
			case (1 << 12):
				// TODO: Logic for including the ECEF Ephemeris field
				break;

			// Formatted INS
			case (1 << 13):
				// TODO: Logic for including the Formatted INS field
				break;

			// Formatted GPS
			case (1 << 14):
				// TODO: Logic for including the Formatted GPS field
				break;

			// Signal Data Packet Payload Format
			case (1 << 15):
				// TODO: Logic for including the Signal Data Packet Payload Format
				break;

			// State/Event Indicators
			case (1 << 16):
				buf[buffer_index++] = htonl(pkt->cif0.state_and_event_indicators);
				break;

			// Device Identifier
			case (1 << 17):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;

				uint32_t word;

				memcpy(&word, &pkt->cif0.device_identifier.lower_word, sizeof(word));
				buf[buffer_index++] = htonl(word);
				memcpy(&word, &pkt->cif0.device_identifier.upper_word, sizeof(word));
				buf[buffer_index++] = htonl(word);

				break;

			// Temperature
			case (1 << 18):
				// Temperature uses a 10.6 fixed-point encoding
				buf[buffer_index++] = htonl((uint32_t)(convert_to_10_6(pkt->cif0.temperature)));
				break;

			// Timestamp Calibration Time
			case (1 << 19):
				buf[buffer_index++] = htonl(pkt->cif0.timestamp_calibration_time_int);
				break;

			// Timestamp Adjustment
			case (1 << 20):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;

				buf[buffer_index++] = htonl((uint32_t)(pkt->cif0.timestamp_adjustment >> 32));
				buf[buffer_index++] = htonl((uint32_t)(pkt->cif0.timestamp_adjustment));

				break;

			// Sample Rate
			case (1 << 21):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;
		
				// Sample Rate uses a 44.20 fixed-point encoding
				int64_t sample_rate = convert_to_44_20(pkt->cif0.sample_rate);

				buf[buffer_index++] = htonl((uint32_t)(sample_rate >> 32));
				buf[buffer_index++] = htonl((uint32_t)(sample_rate));

				break;

			// Over-Range Count
			case (1 << 22):
				buf[buffer_index++] = htonl(pkt->cif0.over_range_count);
				break;

			// Gain
			case (1 << 23):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;
		
				// Gain packs 2 9.7 fixed-point integers into the same 32-bit field.
				// The upper 16 bits represent Stage 2 gain while the lower 16 bits represent Stage 1 gain.

				buf[buffer_index++] = htonl((uint32_t)((convert_to_9_7(pkt->cif0.gain_stage_2) << 16) | (convert_to_9_7(pkt->cif0.gain_stage_1))));
				break;

			// Reference Level
			case (1 << 24):
				// Reference Level uses 9.7 fixed-point encoding
				buf[buffer_index++] = htonl((uint32_t)(convert_to_9_7(pkt->cif0.reference_level)));
				break;

			// IF Band Offset
			case (1 << 25):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;

				// IF Band Offset uses 44.20 fixed-point encoding
				int64_t if_band_offset = convert_to_44_20(pkt->cif0.if_band_offset);

				buf[buffer_index++] = htonl((uint32_t)(if_band_offset >> 32));
				buf[buffer_index++] = htonl((uint32_t)(if_band_offset));

				break;

			// RF Reference Frequency Offset
			case (1 << 26):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;

				// RF Reference Frequency Offset uses 44.20 fixed-point encoding
				int64_t rf_reference_frequency_offset = convert_to_44_20(pkt->cif0.rf_reference_frequency_offset);

				buf[buffer_index++] = htonl((uint32_t)(rf_reference_frequency_offset >> 32));
				buf[buffer_index++] = htonl((uint32_t)(rf_reference_frequency_offset));

				break;

			// RF Reference Frequency
			case (1 << 27):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;

				// RF Reference Frequency uses 44.20 fixed-point encoding
				int64_t rf_reference_frequency = convert_to_44_20(pkt->cif0.rf_reference_frequency);

				buf[buffer_index++] = htonl((uint32_t)(rf_reference_frequency >> 32));
				buf[buffer_index++] = htonl((uint32_t)(rf_reference_frequency));

				break;
			
			// IF Reference Frequency
			case (1 << 28):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;

				// IF Reference Frequency uses 44.20 fixed-point encoding
				int64_t if_reference_frequency = convert_to_44_20(pkt->cif0.if_reference_frequency);

				buf[buffer_index++] = htonl((uint32_t)(if_reference_frequency >> 32));
				buf[buffer_index++] = htonl((uint32_t)(if_reference_frequency));

				break;

			// Bandwidth
			case (1 << 29):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;

				// Bandwidth uses 44.20 fixed-point encoding
				int64_t bandwidth = convert_to_44_20(pkt->cif0.bandwidth);

				buf[buffer_index++] = htonl((uint32_t)(bandwidth >> 32));
				buf[buffer_index++] = htonl((uint32_t)(bandwidth));

				break;

			// Reference Point Identifier
			case (1 << 30):
				buf[buffer_index++] = htonl(pkt->cif0.reference_point_id);

			default:
				break;
		}
	}

	/* Update size in header */
	struct vita49_2_header final_hdr = pkt->prologue.header;
	final_hdr.packet_size_words = buffer_index;
	memcpy(&header_word, &final_hdr, sizeof(uint32_t));
	buf[0] = htonl(header_word);

	return buffer_index;
}

int vita49_2_parse_context_packet(const uint32_t *buf, size_t words, struct vita49_2_context_packet *pkt)
{
	if (!buf || !pkt || words == 0)
		return -EINVAL;

	memset(pkt, 0, sizeof(*pkt));

	/* Header */
	uint32_t header_word = ntohl(buf[0]);
	memcpy(&pkt->prologue.header, &header_word, sizeof(pkt->prologue.header));

	if (pkt->prologue.header.packet_size_words > words)
		return -EINVAL; /* Buffer too small for packet size */

	uint16_t buffer_index = 1;

	/* Determine if Stream ID is present */
	bool has_stream_id = (pkt->prologue.header.packet_type != VITA49_2_PKT_TYPE_IF_DATA_NO_SID &&
						pkt->prologue.header.packet_type != VITA49_2_PKT_TYPE_EXT_DATA_NO_SID);

	if (has_stream_id) 
	{
		if (buffer_index >= pkt->prologue.header.packet_size_words) 
			return -EINVAL;
		
		pkt->prologue.stream_id = ntohl(buf[buffer_index++]);
		pkt->prologue.has_stream_id = true;
	}
	/* Stream ID is required for Context Packets */
	else
	{
		return -1;
	}

	/* Class ID */
	if (pkt->prologue.header.has_class_id) 
	{
		// + 1 since Class ID is 2 words
		if (buffer_index + 1 >= pkt->prologue.header.packet_size_words) 
			return -EINVAL;
		
		uint32_t w1 = ntohl(buf[buffer_index++]);
		uint32_t w2 = ntohl(buf[buffer_index++]);
		
		memcpy(&pkt->prologue.class_id.lower_word, &w1, sizeof(pkt->prologue.class_id.lower_word));
		memcpy(&pkt->prologue.class_id.upper_word, &w2, sizeof(pkt->prologue.class_id.upper_word));

		pkt->prologue.has_class_id = true;
	}

	/* Timestamp Integer */
	if (pkt->prologue.header.ts_integer_format != VITA49_2_TSI_NONE) 
	{
		if (buffer_index >= pkt->prologue.header.packet_size_words) 
			return -EINVAL;
		
		pkt->prologue.timestamp_int = ntohl(buf[buffer_index++]);
		pkt->prologue.has_timestamp_int = true;
	}

	/* Timestamp Fractional */
	if (pkt->prologue.header.ts_fractional_format != VITA49_2_TSF_NONE) 
	{
		// + 1 since Fractional Timestamp is 2 words
		if (buffer_index + 1 >= pkt->prologue.header.packet_size_words) 
			return -EINVAL;

		uint32_t w1 = ntohl(buf[buffer_index++]);
		uint32_t w2 = ntohl(buf[buffer_index++]);
		
		pkt->prologue.timestamp_frac = ((uint64_t)w1 << 32) | w2;
		pkt->prologue.has_timestamp_frac = true;
	}

	/* CIF0 */
	int ret_value;
	if ((ret_value = vita49_2_parse_cif0_payload(pkt->prologue.header.packet_size_words - buffer_index, buf + buffer_index, &pkt->cif0)) < 0)
		return ret_value;

	/* CIF7 */
	if (pkt->cif0.cif0_word.cif7_enable)
	{
		// TODO: Parse the CIF 7 fields and populate the CIF 7 struct
	}

	/* CIF3 */
	if (pkt->cif0.cif0_word.cif3_enable)
	{
		// TODO: Parse the CIF 3 fields and populate the CIF 3 struct
	}

	/* CIF2 */
	if (pkt->cif0.cif0_word.cif2_enable)
	{
		// TODO: Parse the CIF 2 fields and populate the CIF 2 struct
	}

	/* CIF1 */
	if (pkt->cif0.cif0_word.cif1_enable)
	{
		// TODO: Parse the CIF 1 fields and populate the CIF 1 struct
	}

	return 0;
}

ssize_t vita49_2_generate_control_packet(const struct vita49_2_control_packet *pkt, uint32_t *buf, size_t max_words)
{
	if (!pkt || !buf)
		return -EINVAL;

	// The packet size field of the header must be determined before we can write it to the buffer,
	// hence we'll store the header in a local variable and update the appropriate bits once the packet
	// size has been determined at the end.
	uint32_t header_word;

	// Starting the index at 1 since we'll be writing the header (index = 0) last
	size_t buffer_index = 1;

	/* Stream ID */
	if (pkt->command_prologue.common_prologue.has_stream_id) 
	{
		if (buffer_index >= max_words) 
			return -ENOBUFS;
		
		buf[buffer_index++] = htonl(pkt->command_prologue.common_prologue.stream_id);
	}
	// Stream ID is mandatory for any Command-type Packet
	else
	{
		return -1;
	}

	/* Class ID */
	if (pkt->command_prologue.common_prologue.header.has_class_id) 
	{
		// +1 since the Class ID is 2 words
		if (buffer_index + 1 >= max_words) 
			return -ENOBUFS;
		
		uint32_t class_id_word;

		memcpy(&class_id_word, &pkt->command_prologue.common_prologue.class_id.lower_word, sizeof(class_id_word));
		buf[buffer_index++] = htonl(class_id_word);
		
		memcpy(&class_id_word, &pkt->command_prologue.common_prologue.class_id.upper_word, sizeof(class_id_word));
		buf[buffer_index++] = htonl(class_id_word);
	}

	/* Timestamp Int */
	if (pkt->command_prologue.common_prologue.header.ts_integer_format != VITA49_2_TSI_NONE) 
	{
		if (buffer_index >= max_words) 
			return -ENOBUFS;
		
		buf[buffer_index++] = htonl(pkt->command_prologue.common_prologue.timestamp_int);
	}

	/* Timestamp Frac */
	if (pkt->command_prologue.common_prologue.header.ts_fractional_format != VITA49_2_TSF_NONE) 
	{
		// +1 since the fractional timestamp is 2 words
		if (buffer_index + 1 >= max_words) 
			return -ENOBUFS;
		
		buf[buffer_index++] = htonl((uint32_t)(pkt->command_prologue.common_prologue.timestamp_frac >> 32));
		buf[buffer_index++] = htonl((uint32_t)(pkt->command_prologue.common_prologue.timestamp_frac & 0xFFFFFFFF));
	}

	/* CAM */
	if (buffer_index >= max_words)
		return -ENOBUFS;

	uint32_t cam_word;
	memcpy(&cam_word, &pkt->command_prologue.cam, sizeof(cam_word));
	buf[buffer_index++] = htonl(cam_word);	

	/* Message ID */
	if (buffer_index >= max_words)
		return -ENOBUFS;

	buf[buffer_index++] = htonl(pkt->command_prologue.message_id);

	/* Controllee ID/UUID, ADI does not use this field currently however we retain the right to use it in the future */
	if (pkt->command_prologue.cam.has_controllee_id)
	{
		// Using a 128-bit UUID
		if (pkt->command_prologue.cam.controllee_id_format)
		{
			if (buffer_index + 3 >= max_words)
				return -ENOBUFS;

			for (uint8_t i = 0; i < 4; i++)
				buf[buffer_index++] = htonl(pkt->command_prologue.controllee_id.uuid128[i]);
		}
		// Using a 32-bit ID
		else
		{
			if (buffer_index >= max_words)
				return -ENOBUFS;

			buf[buffer_index++] = htonl(pkt->command_prologue.controllee_id.id32);
		}
	}

	/* Controller ID/UUID */
	if (pkt->command_prologue.cam.has_controller_id)
	{
		// Using a 128-bit UUID
		if (pkt->command_prologue.cam.controller_id_format)
		{
			if (buffer_index + 3 >= max_words)
				return -ENOBUFS;

			for (uint8_t i = 0; i < 4; i++)
				buf[buffer_index++] = htonl(pkt->command_prologue.controller_id.uuid128[i]);
		}
		// Using a 32-bit ID
		else
		{
			if (buffer_index >= max_words)
				return -ENOBUFS;

			buf[buffer_index++] = htonl(pkt->command_prologue.controller_id.id32);
		}
	}

	/* Payload (CIF0 and attribute values) */
	uint32_t cif0_word, field_bit;
	memcpy(&cif0_word, &pkt->cif0.cif0_word, sizeof(cif0_word));

	if (buffer_index >= max_words)
		return -ENOBUFS;

	buf[buffer_index++] = htonl(cif0_word);

	// Iterating through each of the 32 options in CIF0 from 31 to 0.
	// "Context Field Indicator" doesn't apply to Command Packets, so we'll actually start from 30.
	for (int field_index = 30; field_index >= 0; field_index--)
	{
		field_bit = (1 << field_index);

		// Field is not present
		if (!(cif0_word & field_bit))
			continue;

		// Checking if there's at least 1 byte left. For fields using more than 1 byte, they will require
		// an additional check in their case-block.
		if (buffer_index >= max_words)
			return -ENOBUFS;

		// See Table 9.1-1 in the VITA 49.2 full spec document. Not all of those fields are applicable to Control Packets.
		switch (field_bit)
		{
			// CIF 1 Enable
			case (1 << 1):
				// TODO: Logic for including the CIF 1 components into this Context Packet
				break;

			// CIF 2 Enable
			case (1 << 2):
				// TODO: Logic for including the CIF 2 components into this Context Packet
				break;
				
			// CIF 3 Enable
			case (1 << 3):
				// TODO: Logic for including the CIF 3 components into this Context Packet
				break;

			// CIF 7 Enable
			case (1 << 7):
				// TODO: Logic for including the CIF 7 components into this Context Packet
				break;

			// Signal Data Packet Payload Format
			case (1 << 15):
				// TODO: Logic for including the Signal Data Packet Payload Format
				break;

			// Sample Rate
			case (1 << 21):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;
		
				// Sample Rate uses a 44.20 fixed-point encoding
				int64_t sample_rate = convert_to_44_20(pkt->cif0.sample_rate);

				buf[buffer_index++] = htonl((uint32_t)(sample_rate >> 32));
				buf[buffer_index++] = htonl((uint32_t)(sample_rate));

				break;

			// Gain
			case (1 << 23):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;
		
				// Gain packs 2 9.7 fixed-point integers into the same 32-bit field.
				// The upper 16 bits represent Stage 2 gain while the lower 16 bits represent Stage 1 gain.

				buf[buffer_index++] = htonl((uint32_t)((convert_to_9_7(pkt->cif0.gain_stage_2) << 16) | (convert_to_9_7(pkt->cif0.gain_stage_1))));
				break;

			// IF Band Offset
			case (1 << 25):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;

				// IF Band Offset uses 44.20 fixed-point encoding
				int64_t if_band_offset = convert_to_44_20(pkt->cif0.if_band_offset);

				buf[buffer_index++] = htonl((uint32_t)(if_band_offset >> 32));
				buf[buffer_index++] = htonl((uint32_t)(if_band_offset));

				break;

			// RF Reference Frequency Offset
			case (1 << 26):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;

				// RF Reference Frequency Offset uses 44.20 fixed-point encoding
				int64_t rf_reference_frequency_offset = convert_to_44_20(pkt->cif0.rf_reference_frequency_offset);

				buf[buffer_index++] = htonl((uint32_t)(rf_reference_frequency_offset >> 32));
				buf[buffer_index++] = htonl((uint32_t)(rf_reference_frequency_offset));

				break;

			// RF Reference Frequency
			case (1 << 27):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;

				// RF Reference Frequency uses 44.20 fixed-point encoding
				int64_t rf_reference_frequency = convert_to_44_20(pkt->cif0.rf_reference_frequency);

				buf[buffer_index++] = htonl((uint32_t)(rf_reference_frequency >> 32));
				buf[buffer_index++] = htonl((uint32_t)(rf_reference_frequency));

				break;
			
			// IF Reference Frequency
			case (1 << 28):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;

				// IF Reference Frequency uses 44.20 fixed-point encoding
				int64_t if_reference_frequency = convert_to_44_20(pkt->cif0.if_reference_frequency);

				buf[buffer_index++] = htonl((uint32_t)(if_reference_frequency >> 32));
				buf[buffer_index++] = htonl((uint32_t)(if_reference_frequency));

				break;

			// Bandwidth
			case (1 << 29):
				if (buffer_index + 1 >= max_words)
					return -ENOBUFS;

				// Bandwidth uses 44.20 fixed-point encoding
				int64_t bandwidth = convert_to_44_20(pkt->cif0.bandwidth);

				buf[buffer_index++] = htonl((uint32_t)(bandwidth >> 32));
				buf[buffer_index++] = htonl((uint32_t)(bandwidth));

				break;

			// Reference Point Identifier
			case (1 << 30):
				buf[buffer_index++] = htonl(pkt->cif0.reference_point_id);

			default:
				break;
		}
	}

	/* Update size in header */
	struct vita49_2_header final_hdr = pkt->command_prologue.common_prologue.header;
	final_hdr.packet_size_words = buffer_index;
	memcpy(&header_word, &final_hdr, sizeof(uint32_t));
	buf[0] = htonl(header_word);

	return buffer_index;
}

int vita49_2_parse_control_packet(const uint32_t *buf, size_t words, struct vita49_2_control_packet *pkt)
{
	if (!buf || !pkt || words == 0)
		return -EINVAL;

	memset(pkt, 0, sizeof(*pkt));

	/* Header */
	uint32_t header_word = ntohl(buf[0]);
	memcpy(&pkt->command_prologue.common_prologue.header, &header_word, sizeof(pkt->command_prologue.common_prologue.header));

	if (pkt->command_prologue.common_prologue.header.packet_size_words > words)
		return -EINVAL; /* Buffer too small for packet size */

	uint16_t buffer_index = 1;

	/* Determine if Stream ID is present */
	bool has_stream_id = (pkt->command_prologue.common_prologue.header.packet_type != VITA49_2_PKT_TYPE_IF_DATA_NO_SID &&
						pkt->command_prologue.common_prologue.header.packet_type != VITA49_2_PKT_TYPE_EXT_DATA_NO_SID);

	if (has_stream_id) 
	{
		if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
			return -EINVAL;
		
		pkt->command_prologue.common_prologue.stream_id = ntohl(buf[buffer_index++]);
		pkt->command_prologue.common_prologue.has_stream_id = true;
	}
	/* Stream ID is required for Command Packets */
	else
	{
		return -1;
	}

	/* Class ID */
	if (pkt->command_prologue.common_prologue.header.has_class_id) 
	{
		// + 1 since Class ID is 2 words
		if (buffer_index + 1 >= pkt->command_prologue.common_prologue.header.packet_size_words) 
			return -EINVAL;
		
		uint32_t w1 = ntohl(buf[buffer_index++]);
		uint32_t w2 = ntohl(buf[buffer_index++]);
		
		memcpy(&pkt->command_prologue.common_prologue.class_id.lower_word, &w1, sizeof(pkt->command_prologue.common_prologue.class_id.lower_word));
		memcpy(&pkt->command_prologue.common_prologue.class_id.upper_word, &w2, sizeof(pkt->command_prologue.common_prologue.class_id.upper_word));

		pkt->command_prologue.common_prologue.has_class_id = true;
	}

	/* Timestamp Integer */
	if (pkt->command_prologue.common_prologue.header.ts_integer_format != VITA49_2_TSI_NONE) 
	{
		if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
			return -EINVAL;
		
		pkt->command_prologue.common_prologue.timestamp_int = ntohl(buf[buffer_index++]);
		pkt->command_prologue.common_prologue.has_timestamp_int = true;
	}

	/* Timestamp Fractional */
	if (pkt->command_prologue.common_prologue.header.ts_fractional_format != VITA49_2_TSF_NONE) 
	{
		// + 1 since Fractional Timestamp is 2 words
		if (buffer_index + 1 >= pkt->command_prologue.common_prologue.header.packet_size_words) 
			return -EINVAL;

		uint32_t w1 = ntohl(buf[buffer_index++]);
		uint32_t w2 = ntohl(buf[buffer_index++]);
		
		pkt->command_prologue.common_prologue.timestamp_frac = ((uint64_t)w1 << 32) | w2;
		pkt->command_prologue.common_prologue.has_timestamp_frac = true;
	}

	/* CAM */
	if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
		return -EINVAL;

	uint32_t cam_word = ntohl(buf[buffer_index++]);
	memcpy(&pkt->command_prologue.cam, &cam_word, sizeof(cam_word));

	/* Message ID */
	if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
		return -EINVAL;

	pkt->command_prologue.message_id = ntohl(buf[buffer_index++]);

	/* Controllee ID */
	// ADI does not use Controllee ID/UUID in the current VITA 49.2 implementation, however we retain the right to use
	// them in the future.

	if (pkt->command_prologue.cam.has_controllee_id)
	{
		// If Controllee ID Format is asserted, that means a 128-bit UUID is used
		if (pkt->command_prologue.cam.controllee_id_format)
		{
			if (buffer_index + 3 >= pkt->command_prologue.common_prologue.header.packet_size_words)
				return -EINVAL;

			for (uint8_t i = 0; i < 4; i++)
			{
				pkt->command_prologue.controllee_id.uuid128[i] = ntohl(buf[buffer_index++]);
			}
		}
		// Otherwise a 32-bit ID is used
		else
		{
			if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words)
				return -EINVAL;

			pkt->command_prologue.controllee_id.id32 = ntohl(buf[buffer_index++]);
		}
	}

	/* Controller ID */
	if (pkt->command_prologue.cam.has_controller_id)
	{
		// If Controller ID Format is asserted, that means a 128-bit UUID is used
		if (pkt->command_prologue.cam.controller_id_format)
		{
			if (buffer_index + 3 >= pkt->command_prologue.common_prologue.header.packet_size_words)
				return -EINVAL;

			for (uint8_t i = 0; i < 4; i++)
			{
				pkt->command_prologue.controller_id.uuid128[i] = ntohl(buf[buffer_index++]);
			}
		}
		// Otherwise a 32-bit ID is used
		else
		{
			if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words)
				return -EINVAL;

			pkt->command_prologue.controller_id.id32 = ntohl(buf[buffer_index++]);
		}
	}

	/* CIF0 */
	int ret_value;
	if ((ret_value = vita49_2_parse_cif0_payload(pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, buf + buffer_index, &pkt->cif0)) < 0)
		return ret_value;

	/* CIF7 */
	if (pkt->cif0.cif0_word.cif7_enable)
	{
		// TODO: Parse the CIF 7 fields and populate the CIF 7 struct
	}

	/* CIF3 */
	if (pkt->cif0.cif0_word.cif3_enable)
	{
		// TODO: Parse the CIF 3 fields and populate the CIF 3 struct
	}

	/* CIF2 */
	if (pkt->cif0.cif0_word.cif2_enable)
	{
		// TODO: Parse the CIF 2 fields and populate the CIF 2 struct
	}

	/* CIF1 */
	if (pkt->cif0.cif0_word.cif1_enable)
	{
		// TODO: Parse the CIF 1 fields and populate the CIF 1 struct
	}

	return 0;
}
