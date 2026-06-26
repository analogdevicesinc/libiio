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
#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif


ssize_t vita49_2_generate_data_packet(struct vita49_2_data_packet *pkt, uint32_t *buf, size_t max_words)
{
	if (!pkt || !buf)
		return -EINVAL;

	// The packet size field of the header must be determined before we can write it to the buffer,
	// hence we'll store the header in a local variable and update the appropriate bits once the packet
	// size has been determined at the end.
	uint32_t header_word;

	// Starting the index at 1 since we'll be writing the header (index = 0) last
	ssize_t buffer_index = 1;

	/* Stream ID */
	bool has_stream_id = (pkt->prologue.header.packet_type != VITA49_2_PKT_TYPE_IF_DATA_NO_SID);

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

		uint32_t payload_word;
		for (uint16_t payload_index = 0; payload_index < pkt->payload_num_words; payload_index++)
		{
			memcpy(&payload_word, &pkt->payload[payload_index], sizeof(payload_word));
			buf[buffer_index + payload_index] = htonl(payload_word);
		}
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
	pkt->prologue.header.packet_size_words = buffer_index;
	memcpy(&header_word, &pkt->prologue.header, sizeof(uint32_t));
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

	// Validating that this is a Data Packet
	if (pkt->prologue.header.packet_type != VITA49_2_PKT_TYPE_IF_DATA_NO_SID && pkt->prologue.header.packet_type != VITA49_2_PKT_TYPE_IF_DATA_WITH_SID)
		return -1;

	uint16_t buffer_index = 1;

	/* Determine if Stream ID is present */
	if (pkt->prologue.header.packet_type == VITA49_2_PKT_TYPE_IF_DATA_WITH_SID) 
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
		
		// According to Figure 5.1.4-1, the most significant word is the first word 
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
		
		pkt->payload_num_words = pkt->prologue.header.packet_size_words - buffer_index - 1;
	} 
	else 
		pkt->payload_num_words = pkt->prologue.header.packet_size_words - buffer_index;

	/* Payload */
	pkt->payload = realloc(NULL, pkt->payload_num_words * sizeof(uint32_t));

	if (pkt->payload == NULL)
		return -ENOMEM;

	uint32_t payload_word;
	for (uint16_t payload_index = 0; payload_index < pkt->payload_num_words; payload_index++)
	{
		payload_word = ntohl(buf[buffer_index + payload_index]);
		memcpy(&pkt->payload[payload_index], &payload_word, sizeof(payload_word));
	}		

	return 0;
}

ssize_t vita49_2_generate_context_packet(struct vita49_2_context_packet *pkt, uint32_t *buf, size_t max_words)
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

	/* Payload (CIF0/1/2/3/7 and attribute values) */
	uint32_t cif0_word, field_bit;
	memcpy(&cif0_word, &pkt->cif0.cif0_word, sizeof(cif0_word));

	if (buffer_index >= max_words)
		return -ENOBUFS;

	buf[buffer_index++] = htonl(cif0_word);

	// Iterating through each of the 32 options in CIF0 from 31 to 0.
	// Butttt, remember that "Context Field Change Indicator" (bit 31) is just an indicator
	// and lacks an actual payload. Hence we'll be going from 30 to 0.
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

		// See Table 9.1-1 in the VITA 49.2 2017 document
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
	pkt->prologue.header.packet_size_words = buffer_index;
	memcpy(&header_word, &pkt->prologue.header, sizeof(uint32_t));
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

	// Validating that this is a Context Packet
	if (pkt->prologue.header.packet_type != VITA49_2_PKT_TYPE_IF_CONTEXT)
		return -EINVAL;

	uint16_t buffer_index = 1;

	/* Stream ID (required for Context Packets) */
	if (buffer_index >= pkt->prologue.header.packet_size_words) 
		return -EINVAL;
		
	pkt->prologue.stream_id = ntohl(buf[buffer_index++]);
	pkt->prologue.has_stream_id = true;

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

	uint32_t cif_word;
	uint8_t cif_word_offset = 0;

	/* CIF0 Word */
	if (vita49_2_get_payload_word(buf + buffer_index, pkt->prologue.header.packet_size_words - buffer_index, cif_word_offset, &cif_word) < 0)
		return -1;

	memcpy(&pkt->cif0.cif0_word, &cif_word, sizeof(cif_word));
	cif_word_offset++;

	/* CIF1 Word */
	if (pkt->cif0.cif0_word.cif1_enable)
	{
		// Checking if memory needs to be allocated for the CIF1 struct
		if (pkt->cif1 == NULL)
		{
			pkt->cif1 = malloc(sizeof(struct vita49_2_cif1_fields));
			if (pkt->cif1 == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->prologue.header.packet_size_words - buffer_index, cif_word_offset, &cif_word) < 0)
			return -1;
		
		// TODO: Need to implement the vita49_2_cif1_fields before I can copy the word into that struct
		// memcpy(&pkt->cif1->cif1_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}

	/* CIF2 Word */
	if (pkt->cif0.cif0_word.cif2_enable)
	{
		// Checking if memory needs to be allocated for the CIF2 struct
		if (pkt->cif2 == NULL)
		{
			pkt->cif2 = malloc(sizeof(struct vita49_2_cif2_fields));
			if (pkt->cif2 == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->prologue.header.packet_size_words - buffer_index, cif_word_offset, cif_word) < 0)
			return -1;

		// TODO: Need to implement the vita49_2_cif2_fields before I can copy the word into that struct
		// memcpy(&pkt->cif2->cif2_word, &cif_word, sizeof(cif_word));
		
		cif_word_offset++;
	}
	
	/* CIF3 Word */
	if (pkt->cif0.cif0_word.cif3_enable)
	{
		// Checking if memory needs to be allocated for the CIF3 struct
		if (pkt->cif3 == NULL)
		{
			pkt->cif3 = malloc(sizeof(struct vita49_2_cif3_fields));
			if (pkt->cif3 == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->prologue.header.packet_size_words - buffer_index, cif_word_offset, cif_word) < 0)
			return -1;

		// TODO: Need to implement the vita49_2_cif3_fields before I can copy the word into that struct
		// memcpy(&pkt->cif3->cif3_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}
	
	/* CIF7 Word */
	if (pkt->cif0.cif0_word.cif7_enable)
	{
		// Checking if memory needs to be allocated for the CIF7 struct
		if (pkt->cif7 == NULL)
		{
			pkt->cif7 = malloc(sizeof(struct vita49_2_cif7_fields));
			if (pkt->cif7 == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->prologue.header.packet_size_words - buffer_index, cif_word_offset, cif_word) < 0)
			return -1;

		// TODO: Need to implement the vita49_2_cif7_fields before I can copy the word into that struct
		// memcpy(&pkt->cif7->cif7_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}


	ssize_t ret_value;
	/* CIF0 Attributes */
	if ((ret_value = vita49_2_parse_cif0_payload(pkt->prologue.header.packet_size_words - buffer_index, buf + buffer_index + cif_word_offset, &pkt->cif0)) < 0)
		return (int)(ret_value);

	/* CIF1 Attributes */
	// TODO: Parse the CIF 1 fields and populate the CIF 1 struct

	/* CIF2 Attributes */
	// TODO: Parse the CIF 2 fields and populate the CIF 2 struct

	/* CIF3 Attributes */
	// TODO: Parse the CIF 3 fields and populate the CIF 3 struct

	/* CIF7 Attributes */
	// TODO: Parse the CIF 7 fields and populate the CIF 7 struct

	return 0;
}

ssize_t vita49_2_generate_control_packet(struct vita49_2_control_packet *pkt, uint32_t *buf, size_t max_words)
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
	memcpy(&cam_word, pkt->command_prologue.control_cam, sizeof(cam_word));
	buf[buffer_index++] = htonl(cam_word);	

	/* Message ID */
	if (buffer_index >= max_words)
		return -ENOBUFS;

	buf[buffer_index++] = htonl(pkt->command_prologue.message_id);

	/* Controllee ID/UUID, ADI does not use this field currently however we retain the right to use it in the future */
	if (pkt->command_prologue.control_cam->has_controllee_id)
	{
		// Using a 128-bit UUID
		if (pkt->command_prologue.control_cam->controllee_id_format)
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
	if (pkt->command_prologue.control_cam->has_controller_id)
	{
		// Using a 128-bit UUID
		if (pkt->command_prologue.control_cam->controller_id_format)
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

		// See Table 9.1-1 in the VITA 49.2 2017 document. Not all of those fields are applicable to Control Packets.
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
	pkt->command_prologue.common_prologue.header.packet_size_words = buffer_index;
	memcpy(&header_word, &pkt->command_prologue.common_prologue.header, sizeof(uint32_t));
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

	// Making sure this is a Command Packet
	if (pkt->command_prologue.common_prologue.header.packet_type != VITA49_2_PKT_TYPE_COMMAND)
		return -EINVAL;

	uint16_t buffer_index = 1;

	/* Stream ID (Requiored for Command Packets)*/	
	if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
		return -EINVAL;
		
	pkt->command_prologue.common_prologue.stream_id = ntohl(buf[buffer_index++]);
	pkt->command_prologue.common_prologue.has_stream_id = true;

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

	// Allocating memory for the CAM if it hasn't already been initialized
	if (pkt->command_prologue.control_cam == NULL)
	{
		pkt->command_prologue.control_cam = malloc(sizeof(*pkt->command_prologue.control_cam));

		if (pkt->command_prologue.control_cam == NULL)
		{
			return -ENOMEM;
		}
	}
	// Ensuring we've allocated enough memory for the word
	else
	{
		void* tmp = realloc(pkt->command_prologue.control_cam, sizeof(*pkt->command_prologue.control_cam));

		if (tmp == NULL)
		{
			return -ENOMEM;
		}

		pkt->command_prologue.control_cam = (struct vita49_2_control_cam_field*)(tmp);
	}

	uint32_t cam_word = ntohl(buf[buffer_index++]);
	memcpy(pkt->command_prologue.control_cam, &cam_word, sizeof(cam_word));

	/* Message ID */
	if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
		return -EINVAL;

	pkt->command_prologue.message_id = ntohl(buf[buffer_index++]);

	/* Controllee ID */
	// ADI does not use Controllee ID/UUID in the current VITA 49.2 implementation, however we retain the right to use
	// them in the future.

	if (pkt->command_prologue.control_cam->has_controllee_id)
	{
		// If Controllee ID Format is asserted, that means a 128-bit UUID is used
		if (pkt->command_prologue.control_cam->controllee_id_format)
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
	if (pkt->command_prologue.control_cam->has_controller_id)
	{
		// If Controller ID Format is asserted, that means a 128-bit UUID is used
		if (pkt->command_prologue.control_cam->controller_id_format)
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

	uint32_t cif_word;
	uint8_t cif_word_offset = 0;
	
	/* CIF0 Word */
	if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, &cif_word) < 0)
	{
		fprintf(stderr, "vita49_2_process: CIF0 word extraction failure while parsing Control Packet.\n");
		return -1;
	}

	memcpy(&pkt->cif0.cif0_word, &cif_word, sizeof(cif_word));
	cif_word_offset++;

	/* CIF1 Word */
	if (pkt->cif0.cif0_word.cif1_enable)
	{
		// Checking if memory needs to be allocated for the CIF1 struct
		if (pkt->cif1 == NULL)
		{
			pkt->cif1 = malloc(sizeof(struct vita49_2_cif1_fields));
			if (pkt->cif1 == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, &cif_word) < 0)
			return -1;
		
		// TODO: Need to implement the vita49_2_cif1_fields before I can copy the word into that struct
		// memcpy(&pkt->cif1->cif1_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}

	/* CIF2 Word */
	if (pkt->cif0.cif0_word.cif2_enable)
	{
		// Checking if memory needs to be allocated for the CIF2 struct
		if (pkt->cif2 == NULL)
		{
			pkt->cif2 = malloc(sizeof(struct vita49_2_cif2_fields));
			if (pkt->cif2 == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, cif_word) < 0)
			return -1;

		// TODO: Need to implement the vita49_2_cif2_fields before I can copy the word into that struct
		// memcpy(&pkt->cif2->cif2_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}
	
	/* CIF3 Word */
	if (pkt->cif0.cif0_word.cif3_enable)
	{
		// Checking if memory needs to be allocated for the CIF3 struct
		if (pkt->cif3 == NULL)
		{
			pkt->cif3 = malloc(sizeof(struct vita49_2_cif3_fields));
			if (pkt->cif3 == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, cif_word) < 0)
			return -1;

		// TODO: Need to implement the vita49_2_cif3_fields before I can copy the word into that struct
		// memcpy(&pkt->cif3->cif3_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}
	
	/* CIF7 Word */
	if (pkt->cif0.cif0_word.cif7_enable)
	{
		// Checking if memory needs to be allocated for the CIF7 struct
		if (pkt->cif7 == NULL)
		{
			pkt->cif7 = malloc(sizeof(struct vita49_2_cif7_fields));
			if (pkt->cif7 == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, cif_word) < 0)
			return -1;
			
		// TODO: Need to implement the vita49_2_cif7_fields before I can copy the word into that struct
		// memcpy(&pkt->cif7->cif7_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}


	int ret_value;
	/* CIF0 Attributes */
	if ((ret_value = vita49_2_parse_cif0_payload(pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, buf + buffer_index + cif_word_offset, &pkt->cif0)) < 0)
		return ret_value;
	
	/* CIF1 Attributes */
	// TODO: Parse the CIF 1 fields and populate the CIF 1 struct

	/* CIF2 Attributes */
	// TODO: Parse the CIF 2 fields and populate the CIF 2 struct

	/* CIF3 Attributes */
	// TODO: Parse the CIF 3 fields and populate the CIF 3 struct

	/* CIF7 Attributes */
	// TODO: Parse the CIF 7 fields and populate the CIF 7 struct

	return 0;
}

__vrt_api ssize_t vita49_2_generate_ackx_packet(struct vita49_2_ackX_packet *pkt, uint32_t *buf, size_t max_words)
{
	if (!pkt || !buf)
		return -EINVAL;

	if (pkt->command_prologue.ack_cam == NULL)
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
	memcpy(&cam_word, pkt->command_prologue.ack_cam, sizeof(cam_word));
	buf[buffer_index++] = htonl(cam_word);	

	/* Message ID */
	if (buffer_index >= max_words)
		return -ENOBUFS;

	buf[buffer_index++] = htonl(pkt->command_prologue.message_id);

	/* Controllee ID/UUID, ADI does not use this field currently however we retain the right to use it in the future */
	if (pkt->command_prologue.ack_cam->has_controllee_id)
	{
		// Using a 128-bit UUID
		if (pkt->command_prologue.ack_cam->controllee_id_format)
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
	if (pkt->command_prologue.ack_cam->has_controller_id)
	{
		// Using a 128-bit UUID
		if (pkt->command_prologue.ack_cam->controller_id_format)
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

	/* Payload (CIF0/1/2/3/7 and attribute values) */
	
	// The Payload is structured as follows:
		// Warning Indicator Fields:
			// CIF0	(32-bit word)
			// CIF1	(32-bit word)
			// CIF2	(32-bit word)
			// CIF3	(32-bit word)
			// CIF7 (32-bit word)
		// Error Indicator Fields:
			// CIF0	(32-bit word)
			// CIF1	(32-bit word)
			// CIF2	(32-bit word)
			// CIF3	(32-bit word)
			// CIF7 (32-bit word)
		// Warning Fields:
			// Warning Indicators associated with CIF0 attributes
			// Warning Indicators associated with CIF1 attributes
			// Warning Indicators associated with CIF2 attributes
			// Warning Indicators associated with CIF3 attributes
			// Warning Indicators associated with CIF7 attributes
		// Error Fields:
			// Error Indicators associated with CIF0 attributes
			// Error Indicators associated with CIF1 attributes
			// Error Indicators associated with CIF2 attributes
			// Error Indicators associated with CIF3 attributes
			// Error Indicators associated with CIF7 attributes
		
	// =========================================================
	// WARNING INDICATOR FIELDS
	// =========================================================

	uint32_t cif_word, field_bit;
	
	if (pkt->command_prologue.ack_cam->warnings_present)
	{
		// CIF0 Word
		memcpy(&cif_word, &pkt->warnings.cif0_warnings, sizeof(cif_word));

		if (buffer_index >= max_words)
			return -ENOBUFS;

		buf[buffer_index++] = htonl(cif_word);

		// CIF1 Word
		if (pkt->warnings.cif0_warnings.cif1_enable && pkt->warnings.cif1_warnings != NULL)
		{
			// TODO: The vita49_2_cif1_fields struct needs to be implemented first,
			// then the logic for copying the CIF1 word can be implemented
		}

		// CIF2 Word
		if (pkt->warnings.cif0_warnings.cif2_enable && pkt->warnings.cif2_warnings != NULL)
		{
			// TODO: The vita49_2_cif2_fields struct needs to be implemented first,
			// then the logic for copying the CIF2 word can be implemented
		}

		// CIF3 Word
		if (pkt->warnings.cif0_warnings.cif3_enable && pkt->warnings.cif3_warnings != NULL)
		{
			// TODO: The vita49_2_cif3_fields struct needs to be implemented first,
			// then the logic for copying the CIF3 word can be implemented
		}

		// CIF7 Word
		if (pkt->warnings.cif0_warnings.cif7_enable && pkt->warnings.cif7_warnings != NULL)
		{
			// TODO: The vita49_2_cif7_fields struct needs to be implemented first,
			// then the logic for copying the CIF7 word can be implemented
		}
	}

	// =========================================================
	// ERROR INDICATOR FIELDS
	// =========================================================
	
	if (pkt->command_prologue.ack_cam->errors_present)
	{
		// CIF0 Word
		memcpy(&cif_word, &pkt->errors.cif0_errors, sizeof(cif_word));

		if (buffer_index >= max_words)
			return -ENOBUFS;

		buf[buffer_index++] = htonl(cif_word);

		// CIF1 Word
		if (pkt->errors.cif0_errors.cif1_enable && pkt->errors.cif1_errors != NULL)
		{
			// TODO: The vita49_2_cif1_fields struct needs to be implemented first,
			// then the logic for copying the CIF1 word can be implemented
		}

		// CIF2 Word
		if (pkt->errors.cif0_errors.cif2_enable && pkt->errors.cif2_errors != NULL)
		{
			// TODO: The vita49_2_cif2_fields struct needs to be implemented first,
			// then the logic for copying the CIF2 word can be implemented
		}

		// CIF3 Word
		if (pkt->errors.cif0_errors.cif3_enable && pkt->errors.cif3_errors != NULL)
		{
			// TODO: The vita49_2_cif3_fields struct needs to be implemented first,
			// then the logic for copying the CIF3 word can be implemented
		}

		// CIF7 Word
		if (pkt->errors.cif0_errors.cif7_enable && pkt->errors.cif7_errors != NULL)
		{
			// TODO: The vita49_2_cif7_fields struct needs to be implemented first,
			// then the logic for copying the CIF7 word can be implemented
		}
	}

	// =========================================================
	// WARNING FIELDS
	// =========================================================

	uint32_t indicator_word;

	if (pkt->command_prologue.ack_cam->warnings_present)
	{
		// CIF0

		// Iterating through the buffer of indicator words and copying them over (also handling byte order translation)
		for (uint16_t i = 0; i < pkt->warnings.warnings_payload_num_words; i++)
		{
			memcpy(&indicator_word, &pkt->warnings.warnings_payload[i], sizeof(indicator_word));
		
			// If the warning indicator is set to 0, that means we cleared it while writing the error indicators at some point,
			// thus this indicator shouldn't be written
			if (indicator_word != 0)
				buf[buffer_index++] = htonl(indicator_word);
		}

		// CIF1 Word
		if (pkt->warnings.cif0_warnings.cif1_enable && pkt->warnings.cif1_warnings != NULL)
		{
			// TODO: The vita49_2_cif1_fields struct needs to be implemented first,
			// then the logic for copying the CIF1 attributes can be implemented
		}

		// CIF2 Word
		if (pkt->warnings.cif0_warnings.cif2_enable && pkt->warnings.cif2_warnings != NULL)
		{
			// TODO: The vita49_2_cif2_fields struct needs to be implemented first,
			// then the logic for copying the CIF2 attributes can be implemented
		}

		// CIF3 Word
		if (pkt->warnings.cif0_warnings.cif3_enable && pkt->warnings.cif3_warnings != NULL)
		{
			// TODO: The vita49_2_cif3_fields struct needs to be implemented first,
			// then the logic for copying the CIF3 attributes can be implemented
		}

		// CIF7 Word
		if (pkt->warnings.cif0_warnings.cif7_enable && pkt->warnings.cif7_warnings != NULL)
		{
			// TODO: The vita49_2_cif7_fields struct needs to be implemented first,
			// then the logic for copying the CIF7 attributes can be implemented
		}
	}

	// =========================================================
	// ERROR FIELDS
	// =========================================================

	if (pkt->command_prologue.ack_cam->errors_present)
	{
		// CIF0

		// Iterating through the buffer of indicator words and copying them over (also handling byte order translation)
		for (uint16_t i = 0; i < pkt->errors.errors_payload_num_words; i++)
		{
			memcpy(&indicator_word, &pkt->errors.errors_payload[i], sizeof(indicator_word));
			buf[buffer_index++] = htonl(indicator_word);
		}

		// CIF1 Word
		if (pkt->errors.cif0_errors.cif1_enable && pkt->errors.cif1_errors != NULL)
		{
			// TODO: The vita49_2_cif1_fields struct needs to be implemented first,
			// then the logic for copying the CIF1 attributes can be implemented
		}

		// CIF2 Word
		if (pkt->errors.cif0_errors.cif2_enable && pkt->errors.cif2_errors != NULL)
		{
			// TODO: The vita49_2_cif2_fields struct needs to be implemented first,
			// then the logic for copying the CIF2 attributes can be implemented
		}

		// CIF3 Word
		if (pkt->errors.cif0_errors.cif3_enable && pkt->errors.cif3_errors != NULL)
		{
			// TODO: The vita49_2_cif3_fields struct needs to be implemented first,
			// then the logic for copying the CIF3 attributes can be implemented
		}

		// CIF7 Word
		if (pkt->errors.cif0_errors.cif7_enable && pkt->errors.cif7_errors != NULL)
		{
			// TODO: The vita49_2_cif7_fields struct needs to be implemented first,
			// then the logic for copying the CIF7 attributes can be implemented
		}
	}

	/* Update size in header */
	pkt->command_prologue.common_prologue.header.packet_size_words = buffer_index;
	memcpy(&header_word, &pkt->command_prologue.common_prologue.header, sizeof(uint32_t));
	buf[0] = htonl(header_word);

	return buffer_index;
}

__vrt_api ssize_t vita49_2_generate_ackv_packet(struct vita49_2_ackV_packet *pkt, uint32_t *buf, size_t max_words)
{
	if (!pkt || !buf)
		return -EINVAL;

	if (pkt->command_prologue.ack_cam == NULL)
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
	memcpy(&cam_word, pkt->command_prologue.ack_cam, sizeof(cam_word));
	buf[buffer_index++] = htonl(cam_word);	

	/* Message ID */
	if (buffer_index >= max_words)
		return -ENOBUFS;

	buf[buffer_index++] = htonl(pkt->command_prologue.message_id);

	/* Controllee ID/UUID, ADI does not use this field currently however we retain the right to use it in the future */
	if (pkt->command_prologue.ack_cam->has_controllee_id)
	{
		// Using a 128-bit UUID
		if (pkt->command_prologue.ack_cam->controllee_id_format)
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
	if (pkt->command_prologue.ack_cam->has_controller_id)
	{
		// Using a 128-bit UUID
		if (pkt->command_prologue.ack_cam->controller_id_format)
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

	/* Payload (CIF0/1/2/3/7 and attribute values) */
	
	// The Payload is structured as follows:
		// Warning Indicator Fields:
			// CIF0	(32-bit word)
			// CIF1	(32-bit word)
			// CIF2	(32-bit word)
			// CIF3	(32-bit word)
			// CIF7 (32-bit word)
		// Error Indicator Fields:
			// CIF0	(32-bit word)
			// CIF1	(32-bit word)
			// CIF2	(32-bit word)
			// CIF3	(32-bit word)
			// CIF7 (32-bit word)
		// Warning Fields:
			// Warning Indicators associated with CIF0 attributes
			// Warning Indicators associated with CIF1 attributes
			// Warning Indicators associated with CIF2 attributes
			// Warning Indicators associated with CIF3 attributes
			// Warning Indicators associated with CIF7 attributes
		// Error Fields:
			// Error Indicators associated with CIF0 attributes
			// Error Indicators associated with CIF1 attributes
			// Error Indicators associated with CIF2 attributes
			// Error Indicators associated with CIF3 attributes
			// Error Indicators associated with CIF7 attributes
		
	// =========================================================
	// WARNING INDICATOR FIELDS
	// =========================================================

	uint32_t cif_word, field_bit;
	
	// CIF0 Word
	if (pkt->command_prologue.ack_cam->warnings_present)
	{
		memcpy(&cif_word, &pkt->warnings.cif0_warnings, sizeof(cif_word));

		if (buffer_index >= max_words)
			return -ENOBUFS;

		buf[buffer_index++] = htonl(cif_word);

		// CIF1 Word
		if (pkt->warnings.cif0_warnings.cif1_enable && pkt->warnings.cif1_warnings != NULL)
		{
			// TODO: The vita49_2_cif1_fields struct needs to be implemented first,
			// then the logic for copying the CIF1 word can be implemented
		}

		// CIF2 Word
		if (pkt->warnings.cif0_warnings.cif2_enable && pkt->warnings.cif2_warnings != NULL)
		{
			// TODO: The vita49_2_cif2_fields struct needs to be implemented first,
			// then the logic for copying the CIF2 word can be implemented
		}

		// CIF3 Word
		if (pkt->warnings.cif0_warnings.cif3_enable && pkt->warnings.cif3_warnings != NULL)
		{
			// TODO: The vita49_2_cif3_fields struct needs to be implemented first,
			// then the logic for copying the CIF3 word can be implemented
		}

		// CIF7 Word
		if (pkt->warnings.cif0_warnings.cif7_enable && pkt->warnings.cif7_warnings != NULL)
		{
			// TODO: The vita49_2_cif7_fields struct needs to be implemented first,
			// then the logic for copying the CIF7 word can be implemented
		}

		// =========================================================
		// WARNING FIELDS
		// =========================================================

		// CIF0
		uint32_t indicator_word;

		// Iterating through the buffer of indicator words and copying them over (also handling byte order translation)
		for (uint16_t i = 0; i < pkt->warnings.warnings_payload_num_words; i++)
		{
			memcpy(&indicator_word, &pkt->warnings.warnings_payload[i], sizeof(indicator_word));
			buf[buffer_index++] = htonl(indicator_word);
		}

		if (pkt->warnings.cif0_warnings.cif1_enable && pkt->warnings.cif1_warnings != NULL)
		{
			// TODO: The vita49_2_cif1_fields struct needs to be implemented first,
			// then the logic for copying the CIF1 attributes can be implemented
		}

		// CIF2 Word
		if (pkt->warnings.cif0_warnings.cif2_enable && pkt->warnings.cif2_warnings != NULL)
		{
			// TODO: The vita49_2_cif2_fields struct needs to be implemented first,
			// then the logic for copying the CIF2 attributes can be implemented
		}

		// CIF3 Word
		if (pkt->warnings.cif0_warnings.cif3_enable && pkt->warnings.cif3_warnings != NULL)
		{
			// TODO: The vita49_2_cif3_fields struct needs to be implemented first,
			// then the logic for copying the CIF3 attributes can be implemented
		}

		// CIF7 Word
		if (pkt->warnings.cif0_warnings.cif7_enable && pkt->warnings.cif7_warnings != NULL)
		{
			// TODO: The vita49_2_cif7_fields struct needs to be implemented first,
			// then the logic for copying the CIF7 attributes can be implemented
		}
	}

	/* Update size in header */
	pkt->command_prologue.common_prologue.header.packet_size_words = buffer_index;
	memcpy(&header_word, &pkt->command_prologue.common_prologue.header, sizeof(uint32_t));
	buf[0] = htonl(header_word);

	return buffer_index;
}

__vrt_api int vita49_2_parse_ackx_packet(const uint32_t *buf, size_t words, struct vita49_2_ackX_packet *pkt)
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

	/* Stream ID (required for Command Packets) */
	if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
		return -EINVAL;
		
	pkt->command_prologue.common_prologue.stream_id = ntohl(buf[buffer_index++]);
	pkt->command_prologue.common_prologue.has_stream_id = true;

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

	// Allocate memory for the CAM
	if (pkt->command_prologue.ack_cam == NULL)
	{
		pkt->command_prologue.ack_cam = malloc(sizeof(*pkt->command_prologue.ack_cam));

		if (pkt->command_prologue.ack_cam == NULL)
			return -ENOMEM;
	}
	// Means memory was already allocated. Just in case it's not enough/more than enough, let's use realloc()
	else
	{
		void *tmp = realloc(pkt->command_prologue.ack_cam, sizeof(struct vita49_2_ack_cam_field));

		if (tmp == NULL)
		{
			free(pkt->command_prologue.ack_cam);
			return -ENOMEM;
		}
	}

	uint32_t cam_word = ntohl(buf[buffer_index++]);
	memcpy(pkt->command_prologue.ack_cam, &cam_word, sizeof(cam_word));

	/* Message ID */
	if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
		return -EINVAL;

	pkt->command_prologue.message_id = ntohl(buf[buffer_index++]);

	/* Controllee ID */
	// ADI does not use Controllee ID/UUID in the current VITA 49.2 implementation, however we retain the right to use
	// them in the future.

	if (pkt->command_prologue.ack_cam->has_controllee_id)
	{
		// If Controllee ID Format is asserted, that means a 128-bit UUID is used
		if (pkt->command_prologue.ack_cam->controllee_id_format)
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
	if (pkt->command_prologue.ack_cam->has_controller_id)
	{
		// If Controller ID Format is asserted, that means a 128-bit UUID is used
		if (pkt->command_prologue.ack_cam->controller_id_format)
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

	// =========================================================
	// WARNING INDICATOR FIELDS
	// =========================================================

	uint32_t cif_word;
	uint8_t cif_word_offset = 0;

	/* CIF0 Word */
	if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, &cif_word) < 0)
		return -1;

	memcpy(&pkt->warnings.cif0_warnings, &cif_word, sizeof(cif_word));
	cif_word_offset++;

	/* CIF1 Word */
	if (pkt->warnings.cif0_warnings.cif1_enable)
	{
		// Checking if memory needs to be allocated for the CIF1 struct
		if (pkt->warnings.cif1_warnings == NULL)
		{
			pkt->warnings.cif1_warnings = malloc(sizeof(struct vita49_2_cif1_fields));
			if (pkt->warnings.cif1_warnings == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, &cif_word) < 0)
			return -1;
		
		// TODO: Need to implement the vita49_2_cif1_fields before I can copy the word into that struct
		// memcpy(&pkt->warnings.cif1_warnings->cif1_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}

	/* CIF2 Word */
	if (pkt->warnings.cif0_warnings.cif2_enable)
	{
		// Checking if memory needs to be allocated for the CIF2 struct
		if (pkt->warnings.cif2_warnings == NULL)
		{
			pkt->warnings.cif2_warnings = malloc(sizeof(struct vita49_2_cif2_fields));
			if (pkt->warnings.cif2_warnings == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, cif_word) < 0)
			return -1;

		// TODO: Need to implement the vita49_2_cif2_fields before I can copy the word into that struct
		// memcpy(&pkt->warnings.cif2_warnings->cif2_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}
	
	/* CIF3 Word */
	if (pkt->warnings.cif0_warnings.cif3_enable)
	{
		// Checking if memory needs to be allocated for the CIF3 struct
		if (pkt->warnings.cif3_warnings == NULL)
		{
			pkt->warnings.cif3_warnings = malloc(sizeof(struct vita49_2_cif3_fields));
			if (pkt->warnings.cif3_warnings == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, cif_word) < 0)
			return -1;

		// TODO: Need to implement the vita49_2_cif3_fields before I can copy the word into that struct
		// memcpy(&pkt->warnings.cif3_warnings->cif3_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}
	
	/* CIF7 Word */
	if (pkt->warnings.cif0_warnings.cif7_enable)
	{
		// Checking if memory needs to be allocated for the CIF7 struct
		if (pkt->warnings.cif7_warnings == NULL)
		{
			pkt->warnings.cif7_warnings = malloc(sizeof(struct vita49_2_cif7_fields));
			if (pkt->warnings.cif7_warnings == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, cif_word) < 0)
			return -1;
			
		// TODO: Need to implement the vita49_2_cif7_fields before I can copy the word into that struct
		// memcpy(&pkt->warnings.cif7_warnings->cif7_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}

	// =========================================================
	// ERROR INDICATOR FIELDS
	// =========================================================

	/* CIF0 Word */
	if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, &cif_word) < 0)
		return -1;

	memcpy(&pkt->errors.cif0_errors, &cif_word, sizeof(cif_word));
	cif_word_offset++;

	/* CIF1 Word */
	if (pkt->warnings.cif0_warnings.cif1_enable)
	{
		// Checking if memory needs to be allocated for the CIF1 struct
		if (pkt->errors.cif1_errors == NULL)
		{
			pkt->errors.cif1_errors = malloc(sizeof(struct vita49_2_cif1_fields));
			if (pkt->errors.cif1_errors == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, &cif_word) < 0)
			return -1;
		
		// TODO: Need to implement the vita49_2_cif1_fields before I can copy the word into that struct
		// memcpy(&pkt->errors.cif1_errors->cif1_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}

	/* CIF2 Word */
	if (pkt->errors.cif0_errors.cif2_enable)
	{
		// Checking if memory needs to be allocated for the CIF2 struct
		if (pkt->errors.cif2_errors == NULL)
		{
			pkt->errors.cif2_errors = malloc(sizeof(struct vita49_2_cif2_fields));
			if (pkt->errors.cif2_errors == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, cif_word) < 0)
			return -1;

		// TODO: Need to implement the vita49_2_cif2_fields before I can copy the word into that struct
		// memcpy(&pkt->errors.cif2_errors->cif2_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}
	
	/* CIF3 Word */
	if (pkt->errors.cif0_errors.cif3_enable)
	{
		// Checking if memory needs to be allocated for the CIF3 struct
		if (pkt->errors.cif3_errors == NULL)
		{
			pkt->errors.cif3_errors = malloc(sizeof(struct vita49_2_cif3_fields));
			if (pkt->errors.cif3_errors == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, cif_word) < 0)
			return -1;

		// TODO: Need to implement the vita49_2_cif3_fields before I can copy the word into that struct
		// memcpy(&pkt->errors.cif3_errors->cif3_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}
	
	/* CIF7 Word */
	if (pkt->errors.cif0_errors.cif7_enable)
	{
		// Checking if memory needs to be allocated for the CIF7 struct
		if (pkt->errors.cif7_errors == NULL)
		{
			pkt->errors.cif7_errors = malloc(sizeof(struct vita49_2_cif7_fields));
			if (pkt->errors.cif7_errors == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, cif_word) < 0)
			return -1;
			
		// TODO: Need to implement the vita49_2_cif7_fields before I can copy the word into that struct
		// memcpy(&pkt->errors.cif7_errors->cif7_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}

	// =========================================================
	// WARNING FIELDS
	// =========================================================
	
	ssize_t cif_payload_offset = 0; // Total CIF offset in 32-bit words, useful for when we have to factor in the offsets from each individual CIF field before passing a total offset value for the Errors Field

	/* CIF0 Attributes */
	ssize_t cif0_payload_offset;
	if ((cif0_payload_offset = vita49_2_parse_cif0_payload(pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, buf + buffer_index + cif_word_offset, &pkt->warnings.cif0_warnings)) < 0)
		return cif0_payload_offset;

	cif_payload_offset += cif0_payload_offset;

	/* CIF1 Attributes */
	ssize_t cif1_payload_offset;
	// TODO: Parse the CIF 1 fields and populate the CIF 1 warnings struct

	/* CIF2 Attributes */
	ssize_t cif2_payload_offset;
	// TODO: Parse the CIF 2 fields and populate the CIF 2 warnings struct

	/* CIF3 Attributes */
	ssize_t cif3_payload_offset;
	// TODO: Parse the CIF 3 fields and populate the CIF 3 warnings struct

	/* CIF7 Attributes */
	ssize_t cif7_payload_offset;
	// TODO: Parse the CIF 7 fields and populate the CIF 7 warnings struct

	// =========================================================
	// ERROR FIELDS
	// =========================================================


	/* CIF0 Attributes */
	if ((cif0_payload_offset = vita49_2_parse_cif0_payload(pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, buf + buffer_index + cif_word_offset + cif_payload_offset, &pkt->errors.cif0_errors)) < 0)
		return cif0_payload_offset;

	cif_payload_offset += cif0_payload_offset;

	/* CIF1 Attributes */
	// TODO: Parse the CIF 1 fields and populate the CIF 1 errors struct

	/* CIF2 Attributes */
	// TODO: Parse the CIF 2 fields and populate the CIF 2 errors struct

	/* CIF3 Attributes */
	// TODO: Parse the CIF 3 fields and populate the CIF 3 errors struct

	/* CIF7 Attributes */
	// TODO: Parse the CIF 7 fields and populate the CIF 7 errors struct

	return 0;
}

__vrt_api int vita49_2_parse_ackv_packet(const uint32_t *buf, size_t words, struct vita49_2_ackV_packet *pkt)
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

	/* Stream ID (required for Command Packets) */
	if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
		return -EINVAL;
		
	pkt->command_prologue.common_prologue.stream_id = ntohl(buf[buffer_index++]);
	pkt->command_prologue.common_prologue.has_stream_id = true;

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

	// Allocate memory for the CAM
	if (pkt->command_prologue.ack_cam == NULL)
	{
		pkt->command_prologue.ack_cam = malloc(sizeof(*pkt->command_prologue.ack_cam));

		if (pkt->command_prologue.ack_cam == NULL)
			return -ENOMEM;
	}
	// Means memory was already allocated. Just in case it's not enough/more than enough, let's use realloc()
	else
	{
		void *tmp = realloc(pkt->command_prologue.ack_cam, sizeof(struct vita49_2_ack_cam_field));

		if (tmp == NULL)
		{
			free(pkt->command_prologue.ack_cam);
			return -ENOMEM;
		}
	}

	uint32_t cam_word = ntohl(buf[buffer_index++]);
	memcpy(pkt->command_prologue.ack_cam, &cam_word, sizeof(cam_word));

	/* Message ID */
	if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
		return -EINVAL;

	pkt->command_prologue.message_id = ntohl(buf[buffer_index++]);

	/* Controllee ID */
	// ADI does not use Controllee ID/UUID in the current VITA 49.2 implementation, however we retain the right to use
	// them in the future.

	if (pkt->command_prologue.ack_cam->has_controllee_id)
	{
		// If Controllee ID Format is asserted, that means a 128-bit UUID is used
		if (pkt->command_prologue.ack_cam->controllee_id_format)
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
	if (pkt->command_prologue.ack_cam->has_controller_id)
	{
		// If Controller ID Format is asserted, that means a 128-bit UUID is used
		if (pkt->command_prologue.ack_cam->controller_id_format)
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

	// =========================================================
	// WARNING INDICATOR FIELDS
	// =========================================================

	uint32_t cif_word;
	uint8_t cif_word_offset = 0;

	/* CIF0 Word */
	if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, &cif_word) < 0)
		return -1;

	memcpy(&pkt->warnings.cif0_warnings, &cif_word, sizeof(cif_word));
	cif_word_offset++;

	/* CIF1 Word */
	if (pkt->warnings.cif0_warnings.cif1_enable)
	{
		// Checking if memory needs to be allocated for the CIF1 struct
		if (pkt->warnings.cif1_warnings == NULL)
		{
			pkt->warnings.cif1_warnings = malloc(sizeof(struct vita49_2_cif1_fields));
			if (pkt->warnings.cif1_warnings == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, &cif_word) < 0)
			return -1;
		
		// TODO: Need to implement the vita49_2_cif1_fields before I can copy the word into that struct
		// memcpy(&pkt->warnings.cif1_warnings->cif1_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}

	/* CIF2 Word */
	if (pkt->warnings.cif0_warnings.cif2_enable)
	{
		// Checking if memory needs to be allocated for the CIF2 struct
		if (pkt->warnings.cif2_warnings == NULL)
		{
			pkt->warnings.cif2_warnings = malloc(sizeof(struct vita49_2_cif2_fields));
			if (pkt->warnings.cif2_warnings == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, cif_word) < 0)
			return -1;

		// TODO: Need to implement the vita49_2_cif2_fields before I can copy the word into that struct
		// memcpy(&pkt->warnings.cif2_warnings->cif2_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}
	
	/* CIF3 Word */
	if (pkt->warnings.cif0_warnings.cif3_enable)
	{
		// Checking if memory needs to be allocated for the CIF3 struct
		if (pkt->warnings.cif3_warnings == NULL)
		{
			pkt->warnings.cif3_warnings = malloc(sizeof(struct vita49_2_cif3_fields));
			if (pkt->warnings.cif3_warnings == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, cif_word) < 0)
			return -1;

		// TODO: Need to implement the vita49_2_cif3_fields before I can copy the word into that struct
		// memcpy(&pkt->warnings.cif3_warnings->cif3_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}
	
	/* CIF7 Word */
	if (pkt->warnings.cif0_warnings.cif7_enable)
	{
		// Checking if memory needs to be allocated for the CIF7 struct
		if (pkt->warnings.cif7_warnings == NULL)
		{
			pkt->warnings.cif7_warnings = malloc(sizeof(struct vita49_2_cif7_fields));
			if (pkt->warnings.cif7_warnings == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, cif_word) < 0)
			return -1;
			
		// TODO: Need to implement the vita49_2_cif7_fields before I can copy the word into that struct
		// memcpy(&pkt->warnings.cif7_warnings->cif7_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}

	// =========================================================
	// WARNING FIELDS
	// =========================================================
	
	ssize_t cif_payload_offset = 0; // Total CIF offset in 32-bit words, useful for when we have to factor in the offsets from each individual CIF field before passing a total offset value for the Errors Field

	/* CIF0 Attributes */
	ssize_t cif0_payload_offset;
	if ((cif0_payload_offset = vita49_2_parse_cif0_payload(pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, buf + buffer_index + cif_word_offset, &pkt->warnings.cif0_warnings)) < 0)
		return cif0_payload_offset;

	cif_payload_offset += cif0_payload_offset;

	/* CIF1 Attributes */
	ssize_t cif1_payload_offset;
	// TODO: Parse the CIF 1 fields and populate the CIF 1 warnings struct

	/* CIF2 Attributes */
	ssize_t cif2_payload_offset;
	// TODO: Parse the CIF 2 fields and populate the CIF 2 warnings struct

	/* CIF3 Attributes */
	ssize_t cif3_payload_offset;
	// TODO: Parse the CIF 3 fields and populate the CIF 3 warnings struct

	/* CIF7 Attributes */
	ssize_t cif7_payload_offset;
	// TODO: Parse the CIF 7 fields and populate the CIF 7 warnings struct

	return 0;
}


__vrt_api ssize_t vita49_2_generate_acks_packet(struct vita49_2_ackS_packet *pkt, uint32_t *buf, size_t max_words)
{
	if (!pkt || !buf)
		return -EINVAL;

	if (pkt->command_prologue.ack_cam == NULL)
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
	memcpy(&cam_word, pkt->command_prologue.ack_cam, sizeof(cam_word));
	buf[buffer_index++] = htonl(cam_word);	

	/* Message ID */
	if (buffer_index >= max_words)
		return -ENOBUFS;

	buf[buffer_index++] = htonl(pkt->command_prologue.message_id);

	/* Controllee ID/UUID, ADI does not use this field currently however we retain the right to use it in the future */
	if (pkt->command_prologue.ack_cam->has_controllee_id)
	{
		// Using a 128-bit UUID
		if (pkt->command_prologue.ack_cam->controllee_id_format)
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
	if (pkt->command_prologue.ack_cam->has_controller_id)
	{
		// Using a 128-bit UUID
		if (pkt->command_prologue.ack_cam->controller_id_format)
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

	/* Payload (CIF0/1/2/3/7 and attribute values) */
	/* CIF0 Word */
	uint32_t cif_word, field_bit;
	memcpy(&cif_word, &pkt->cif0.cif0_word, sizeof(cif_word));

	if (buffer_index >= max_words)
		return -ENOBUFS;

	buf[buffer_index++] = htonl(cif_word);

	/* CIF1 Word */
	if (pkt->cif0.cif0_word.cif1_enable)
	{
		if (buffer_index >= max_words)
			return -ENOBUFS;

		memcpy(&cif_word, &pkt->cif1->cif1_word, sizeof(cif_word));
		buf[buffer_index++] = htonl(cif_word);
	}

	/* CIF2 Word */
	if (pkt->cif0.cif0_word.cif2_enable)
	{
		if (buffer_index >= max_words)
			return -ENOBUFS;

		memcpy(&cif_word, &pkt->cif2->cif2_word, sizeof(cif_word));
		buf[buffer_index++] = htonl(cif_word);
	}

	/* CIF3 Word */
	if (pkt->cif0.cif0_word.cif3_enable)
	{
		if (buffer_index >= max_words)
			return -ENOBUFS;

		memcpy(&cif_word, &pkt->cif3->cif3_word, sizeof(cif_word));
		buf[buffer_index++] = htonl(cif_word);
	}

	/* CIF7 Word */
	if (pkt->cif0.cif0_word.cif7_enable)
	{
		if (buffer_index >= max_words)
			return -ENOBUFS;

		memcpy(&cif_word, &pkt->cif7->cif7_word, sizeof(cif_word));
		buf[buffer_index++] = htonl(cif_word);
	}

	/* CIF0 Attribute Values */

	memcpy(&cif_word, &pkt->cif0.cif0_word, sizeof(cif_word));

	// Iterating through each of the 32 options in CIF0 from 31 to 0.
	// Butttt, remember that "Context Field Change Indicator" (bit 31) is just an indicator
	// and lacks an actual payload. Hence we'll be going from 30 to 0.
	for (int field_index = 30; field_index >= 0; field_index--)
	{
		field_bit = (1 << field_index);

		// Field is not present
		if (!(cif_word & field_bit))
			continue;

		// Checking if there's at least 1 byte left. For fields using more than 1 byte, they will require
		// an additional check in their case-block.
		if (buffer_index >= max_words)
			return -ENOBUFS;

		// See Table 9.1-1 in the VITA 49.2 2017 document
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

	/* CIF1 Attribute Values */
	// TODO: Logic for copying the CIF1 values

	/* CIF2 Attribute Values */
	// TODO: Logic for copying the CIF2 values

	/* CIF3 Attribute Values */
	// TODO: Logic for copying the CIF3 values

	/* CIF7 Attribute Values */
	// TODO: Logic for copying the CIF7 values

	/* Update size in header */
	pkt->command_prologue.common_prologue.header.packet_size_words = buffer_index;
	memcpy(&header_word, &pkt->command_prologue.common_prologue.header, sizeof(uint32_t));
	buf[0] = htonl(header_word);

	return buffer_index;
}


__vrt_api int vita49_2_parse_acks_packet(const uint32_t *buf, size_t words, struct vita49_2_ackS_packet *pkt)
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

	/* Stream ID (required for Command Packets) */
	if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
		return -EINVAL;
		
	pkt->command_prologue.common_prologue.stream_id = ntohl(buf[buffer_index++]);
	pkt->command_prologue.common_prologue.has_stream_id = true;

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

	// Allocate memory for the CAM
	if (pkt->command_prologue.ack_cam == NULL)
	{
		pkt->command_prologue.ack_cam = malloc(sizeof(*pkt->command_prologue.ack_cam));

		if (pkt->command_prologue.ack_cam == NULL)
			return -ENOMEM;
	}
	// Means memory was already allocated. Just in case it's not enough/more than enough, let's use realloc()
	else
	{
		void *tmp = realloc(pkt->command_prologue.ack_cam, sizeof(struct vita49_2_ack_cam_field));

		if (tmp == NULL)
		{
			free(pkt->command_prologue.ack_cam);
			return -ENOMEM;
		}
	}

	uint32_t cam_word = ntohl(buf[buffer_index++]);
	memcpy(pkt->command_prologue.ack_cam, &cam_word, sizeof(cam_word));

	/* Message ID */
	if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
		return -EINVAL;

	pkt->command_prologue.message_id = ntohl(buf[buffer_index++]);

	/* Controllee ID */
	// ADI does not use Controllee ID/UUID in the current VITA 49.2 implementation, however we retain the right to use
	// them in the future.

	if (pkt->command_prologue.ack_cam->has_controllee_id)
	{
		// If Controllee ID Format is asserted, that means a 128-bit UUID is used
		if (pkt->command_prologue.ack_cam->controllee_id_format)
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
	if (pkt->command_prologue.ack_cam->has_controller_id)
	{
		// If Controller ID Format is asserted, that means a 128-bit UUID is used
		if (pkt->command_prologue.ack_cam->controller_id_format)
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

	uint32_t cif_word;
	uint8_t cif_word_offset = 0;

	/* CIF0 Word */
	if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, &cif_word) < 0)
		return -1;

	memcpy(&pkt->cif0.cif0_word, &cif_word, sizeof(cif_word));
	cif_word_offset++;

	/* CIF1 Word */
	if (pkt->cif0.cif0_word.cif1_enable)
	{
		// Checking if memory needs to be allocated for the CIF1 struct
		if (pkt->cif1 == NULL)
		{
			pkt->cif1 = malloc(sizeof(struct vita49_2_cif1_fields));
			if (pkt->cif1 == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, &cif_word) < 0)
			return -1;
		
		// TODO: Need to implement the vita49_2_cif1_fields before I can copy the word into that struct
		// memcpy(&pkt->cif1->cif1_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}

	/* CIF2 Word */
	if (pkt->cif0.cif0_word.cif2_enable)
	{
		// Checking if memory needs to be allocated for the CIF2 struct
		if (pkt->cif2 == NULL)
		{
			pkt->cif2 = malloc(sizeof(struct vita49_2_cif2_fields));
			if (pkt->cif2 == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, cif_word) < 0)
			return -1;

		// TODO: Need to implement the vita49_2_cif2_fields before I can copy the word into that struct
		// memcpy(&pkt->cif2->cif2_word, &cif_word, sizeof(cif_word));
		
		cif_word_offset++;
	}
	
	/* CIF3 Word */
	if (pkt->cif0.cif0_word.cif3_enable)
	{
		// Checking if memory needs to be allocated for the CIF3 struct
		if (pkt->cif3 == NULL)
		{
			pkt->cif3 = malloc(sizeof(struct vita49_2_cif3_fields));
			if (pkt->cif3 == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, cif_word) < 0)
			return -1;

		// TODO: Need to implement the vita49_2_cif3_fields before I can copy the word into that struct
		// memcpy(&pkt->cif3->cif3_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}
	
	/* CIF7 Word */
	if (pkt->cif0.cif0_word.cif7_enable)
	{
		// Checking if memory needs to be allocated for the CIF7 struct
		if (pkt->cif7 == NULL)
		{
			pkt->cif7 = malloc(sizeof(struct vita49_2_cif7_fields));
			if (pkt->cif7 == NULL)
				return -ENOMEM;
		}
		
		if (vita49_2_get_payload_word(buf + buffer_index, pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, cif_word_offset, cif_word) < 0)
			return -1;

		// TODO: Need to implement the vita49_2_cif7_fields before I can copy the word into that struct
		// memcpy(&pkt->cif7->cif7_word, &cif_word, sizeof(cif_word));

		cif_word_offset++;
	}


	ssize_t ret_value;
	
	/* CIF0 Attributes */
	if ((ret_value = vita49_2_parse_cif0_payload(pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, buf + buffer_index + cif_word_offset, &pkt->cif0)) < 0)
		return (int)(ret_value);

	/* CIF1 Attributes */
	// TODO: Parse the CIF 1 fields and populate the CIF 1 struct

	/* CIF2 Attributes */
	// TODO: Parse the CIF 2 fields and populate the CIF 2 struct

	/* CIF3 Attributes */
	// TODO: Parse the CIF 3 fields and populate the CIF 3 struct

	/* CIF7 Attributes */
	// TODO: Parse the CIF 7 fields and populate the CIF 7 struct

	return 0;
}