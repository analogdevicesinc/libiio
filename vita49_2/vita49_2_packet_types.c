/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Praveen Perera <praveen.perera@analog.com>
 * 
 * Contributors:
 * 		- Travis Collins <travis.collins@analog.com>
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


__vrt_api ssize_t vita49_2_serialize_data_packet(struct vita49_2_data_packet* const pkt, uint32_t* const buf, size_t max_words)
{
	if (pkt == NULL || buf == NULL || max_words == 0)
		return -EINVAL;

	ssize_t ret_value;

	// Common Prologue
	if ((ret_value = vita49_2_serialize_common_prologue(&pkt->prologue, buf, max_words)) < 0)
		return ret_value;

	ssize_t buffer_index = ret_value;

	// Payload
	if (pkt->payload && pkt->payload_num_words > 0) 
	{
		// Checking if we have enough space in the buffer for the payload, as well as the trailer (if applicable)
		if ((buffer_index + pkt->payload_num_words) > (max_words - (pkt->has_trailer ? 1 : 0))) 
			return -ENOBUFS;

		for (uint16_t payload_index = 0; payload_index < pkt->payload_num_words; payload_index++)
		{
			buf[buffer_index + payload_index] = htonl(pkt->payload[payload_index].word);
		}
		buffer_index += pkt->payload_num_words;
	}

	// Trailer
	if (pkt->has_trailer) 
	{
		if (buffer_index >= max_words) 
			return -ENOBUFS;
		
		buf[buffer_index++] = htonl(pkt->trailer.word);
	}

	// Update packet size in header
	pkt->prologue.header.packet_size_words = buffer_index;
	buf[0] = htonl(pkt->prologue.header.word);

	return buffer_index;
}

__vrt_api int vita49_2_parse_data_packet(const uint32_t* const buf, size_t buf_words, struct vita49_2_data_packet* const pkt, bool with_stream_id)
{
	if (buf == NULL || pkt == NULL || buf_words == 0)
		return -EINVAL;

	memset(pkt, 0, sizeof(*pkt));
	ssize_t buffer_index;

	// Prologue
	if ((buffer_index = vita49_2_parse_common_prologue(buf, buf_words, &pkt->prologue, with_stream_id ? VITA49_2_PKT_TYPE_IF_DATA_WITH_SID : VITA49_2_PKT_TYPE_IF_DATA_NO_SID)) < 0)
		return buffer_index;
	
	// Trailer
	if (pkt->has_trailer) 
	{
		if (pkt->prologue.header.packet_size_words < buffer_index + 1) 
			return -EINVAL;
		
		pkt->trailer.word = ntohl(buf[pkt->prologue.header.packet_size_words - 1]);
		pkt->has_trailer = true;
		
		pkt->payload_num_words = pkt->prologue.header.packet_size_words - buffer_index - 1;
	} 
	else 
		pkt->payload_num_words = pkt->prologue.header.packet_size_words - buffer_index;

	// Payload
	if ((buffer_index + pkt->payload_num_words - 1) >= buf_words)
		return -EINVAL;

	void* tmp = realloc(NULL, pkt->payload_num_words * sizeof(uint32_t));
	if (tmp == NULL)
		return -ENOMEM;

	pkt->payload = tmp;

	for (uint16_t payload_index = 0; payload_index < pkt->payload_num_words; payload_index++)
	{
		pkt->payload[payload_index].word = ntohl(buf[buffer_index++]);
	}		

	return 0;
}

__vrt_api ssize_t vita49_2_serialize_context_packet(struct vita49_2_context_packet* const pkt, uint32_t* const buf, size_t max_words)
{
	if (pkt == NULL || buf == NULL || max_words == 0)
		return -EINVAL;

	ssize_t buffer_index, ret_value;

	// Common Prologue
	if ((buffer_index = vita49_2_serialize_common_prologue(&pkt->prologue, buf, max_words)) < 0)
		return buffer_index;

	// Payload (CIF Words + CIF Attribute Data)
	if ((ret_value = vita49_2_serialize_cif_fields(&pkt->cif0, pkt->cif1, pkt->cif2, pkt->cif3, pkt->cif7, buf, buffer_index, max_words, false)) < 0)
		return ret_value;

	buffer_index += ret_value;

	// Update packet size in header
	pkt->prologue.header.packet_size_words = buffer_index;
	buf[0] = htonl(pkt->prologue.header.word);

	return buffer_index;
}

__vrt_api int vita49_2_parse_context_packet(const uint32_t* const buf, size_t buf_words, struct vita49_2_context_packet* const pkt)
{
	if (buf == NULL || pkt == NULL || buf_words == 0)
		return -EINVAL;

	memset(pkt, 0, sizeof(*pkt));
	ssize_t buffer_index;

	// Prologue
	if ((buffer_index = vita49_2_parse_common_prologue(buf, buf_words, &pkt->prologue, VITA49_2_PKT_TYPE_IF_CONTEXT)) < 0)
		return buffer_index;

	// Payload (CIF Fields + Attributes)
	if ((buffer_index = vita49_2_parse_cif_fields(pkt->prologue.header.packet_size_words - buffer_index, buf, buffer_index, &pkt->cif0, pkt->cif1, pkt->cif2, pkt->cif3, pkt->cif7)) < 0)
		return buffer_index;

	return 0;
}

__vrt_api ssize_t vita49_2_serialize_control_packet(struct vita49_2_control_packet* const pkt, uint32_t* const buf, size_t max_words)
{
	if (pkt == NULL || buf == NULL || max_words == 0)
		return -EINVAL;

	if (pkt->command_prologue.control_cam == NULL)
		return -EINVAL;

	ssize_t buffer_index, ret_value;

	// Command Prologue
	if ((buffer_index = vita49_2_serialize_command_prologue(&pkt->command_prologue, buf, max_words, true)) < 0)
		return buffer_index;

	// Payload (CIF Words + CIF Attribute Data)
	if ((ret_value = vita49_2_serialize_cif_fields(&pkt->cif0, pkt->cif1, pkt->cif2, pkt->cif3, pkt->cif7, buf, buffer_index, max_words, true)) < 0)
		return ret_value;

	buffer_index += ret_value;

	// Update packet size in header
	pkt->command_prologue.common_prologue.header.packet_size_words = buffer_index;
	buf[0] = htonl(pkt->command_prologue.common_prologue.header.word);

	return buffer_index;
}

__vrt_api int vita49_2_parse_control_packet(const uint32_t* const buf, size_t buf_words, struct vita49_2_control_packet* const pkt)
{
	if (buf == NULL || pkt == NULL || buf_words == 0)
		return -EINVAL;

	memset(pkt, 0, sizeof(*pkt));
	ssize_t buffer_index;

	// Command Prologue
	if ((buffer_index = vita49_2_parse_command_prologue(buf, buf_words, &pkt->command_prologue, VITA49_2_PKT_TYPE_COMMAND)) < 0)
		return buffer_index;
	
	// Payload (CIF Fields + Attributes)
	if ((buffer_index = vita49_2_parse_cif_fields(pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, buf, buffer_index, &pkt->cif0, pkt->cif1, pkt->cif2, pkt->cif3, pkt->cif7)) < 0)
		return buffer_index;

	return 0;
}

__vrt_api ssize_t vita49_2_serialize_ackX_packet(struct vita49_2_ackX_packet* const pkt, uint32_t* const buf, size_t max_words)
{
	if (pkt == NULL || buf == NULL || max_words == 0)
		return -EINVAL;

	if (pkt->command_prologue.ack_cam == NULL)
		return -EINVAL;

	ssize_t buffer_index;

	// Command Prologue
	if ((buffer_index = vita49_2_serialize_command_prologue(&pkt->command_prologue, buf, max_words, false)) < 0)
		return buffer_index;

	// Payload (Warning/Error Indicator Fields + Warning/Error Indicators)
	{
	
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
		
		if (pkt->command_prologue.ack_cam->warnings_present)
		{
			// CIF0 Word

			if (buffer_index >= max_words)
				return -ENOBUFS;

			buf[buffer_index++] = htonl(pkt->warnings.cif0_warnings.word);

			// CIF1 Word
			if (pkt->warnings.cif0_warnings.cif1_enable && pkt->warnings.cif1_warnings != NULL)
			{
				if (buffer_index >= max_words)
					return -ENOBUFS;

				buf[buffer_index++] = htonl(pkt->warnings.cif1_warnings->word);
			}

			// CIF2 Word
			if (pkt->warnings.cif0_warnings.cif2_enable && pkt->warnings.cif2_warnings != NULL)
			{
				if (buffer_index >= max_words)
					return -ENOBUFS;

				buf[buffer_index++] = htonl(pkt->warnings.cif2_warnings->word);
			}

			// CIF3 Word
			if (pkt->warnings.cif0_warnings.cif3_enable && pkt->warnings.cif3_warnings != NULL)
			{
				if (buffer_index >= max_words)
					return -ENOBUFS;

				buf[buffer_index++] = htonl(pkt->warnings.cif3_warnings->word);
			}

			// CIF7 Word
			if (pkt->warnings.cif0_warnings.cif7_enable && pkt->warnings.cif7_warnings != NULL)
			{
				if (buffer_index >= max_words)
					return -ENOBUFS;

				buf[buffer_index++] = htonl(pkt->warnings.cif7_warnings->word);
			}
		}

		// =========================================================
		// ERROR INDICATOR FIELDS
		// =========================================================
		
		if (pkt->command_prologue.ack_cam->errors_present)
		{
			// CIF0 Word
			if (buffer_index >= max_words)
				return -ENOBUFS;

			buf[buffer_index++] = htonl(pkt->errors.cif0_errors.word);

			// CIF1 Word
			if (pkt->errors.cif0_errors.cif1_enable && pkt->errors.cif1_errors != NULL)
			{
				if (buffer_index >= max_words)
					return -ENOBUFS;

				buf[buffer_index++] = htonl(pkt->errors.cif1_errors->word);
			}

			// CIF2 Word
			if (pkt->errors.cif0_errors.cif2_enable && pkt->errors.cif2_errors != NULL)
			{
				if (buffer_index >= max_words)
					return -ENOBUFS;

				buf[buffer_index++] = htonl(pkt->errors.cif2_errors->word);
			}

			// CIF3 Word
			if (pkt->errors.cif0_errors.cif3_enable && pkt->errors.cif3_errors != NULL)
			{
				if (buffer_index >= max_words)
					return -ENOBUFS;

				buf[buffer_index++] = htonl(pkt->errors.cif3_errors->word);
			}

			// CIF7 Word
			if (pkt->errors.cif0_errors.cif7_enable && pkt->errors.cif7_errors != NULL)
			{
				if (buffer_index >= max_words)
					return -ENOBUFS;

				buf[buffer_index++] = htonl(pkt->errors.cif7_errors->word);
			}
		}

		// =========================================================
		// WARNING FIELDS
		// =========================================================

		if (pkt->command_prologue.ack_cam->warnings_present)
		{
			// CIF0

			// Iterating through the buffer of indicator words and copying them over (also handling byte order translation)
			for (uint16_t i = 0; i < pkt->warnings.warnings_payload_num_words; i++)
			{
				// If the warning indicator is set to 0, that means we cleared it while writing the error indicators at some point,
				// thus this indicator shouldn't be written
				if (pkt->warnings.warnings_payload[i].word != 0)
					buf[buffer_index++] = htonl(pkt->warnings.warnings_payload[i].word);
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
				buf[buffer_index++] = htonl(pkt->errors.errors_payload[i].word);
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
	}

	// Update packet size in header
	pkt->command_prologue.common_prologue.header.packet_size_words = buffer_index;
	buf[0] = htonl(pkt->command_prologue.common_prologue.header.word);

	return buffer_index;
}

__vrt_api int vita49_2_parse_ackX_packet(const uint32_t* const buf, size_t buf_words, struct vita49_2_ackX_packet* const pkt)
{
	if (buf == NULL || pkt == NULL || buf_words == 0)
		return -EINVAL;

	memset(pkt, 0, sizeof(*pkt));
	ssize_t buffer_index, ret_value;

	// Command Prologue
	if ((buffer_index = vita49_2_parse_command_prologue(buf, buf_words, &pkt->command_prologue, VITA49_2_PKT_TYPE_COMMAND)) < 0)
		return buffer_index;

	// Payload (CIF0/1/2/3/7 and attribute values)
	{
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
		
		if (pkt->command_prologue.ack_cam->warnings_present)
		{
			// CIF0 Word
			if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
				return -EINVAL;

			pkt->warnings.cif0_warnings.word = htonl(buf[buffer_index++]);

			// CIF1 Word
			if (pkt->warnings.cif0_warnings.cif1_enable && pkt->warnings.cif1_warnings != NULL)
			{
				if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
					return -EINVAL;

				// Checking if memory needs to be allocated for the CIF1 struct
				if (pkt->warnings.cif1_warnings == NULL)
				{
					pkt->warnings.cif1_warnings = malloc(sizeof(struct vita49_2_cif1_fields));
					if (pkt->warnings.cif1_warnings == NULL)
						return -ENOMEM;
				}

				pkt->warnings.cif1_warnings->word = htonl(buf[buffer_index++]);
			}

			// CIF2 Word
			if (pkt->warnings.cif0_warnings.cif2_enable && pkt->warnings.cif2_warnings != NULL)
			{
				if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
					return -EINVAL;

				// Checking if memory needs to be allocated for the CIF1 struct
				if (pkt->warnings.cif2_warnings == NULL)
				{
					pkt->warnings.cif2_warnings = malloc(sizeof(struct vita49_2_cif2_fields));
					if (pkt->warnings.cif2_warnings == NULL)
						return -ENOMEM;
				}

				pkt->warnings.cif2_warnings->word = htonl(buf[buffer_index++]);
			}

			// CIF3 Word
			if (pkt->warnings.cif0_warnings.cif3_enable && pkt->warnings.cif3_warnings != NULL)
			{
				if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
					return -EINVAL;

				// Checking if memory needs to be allocated for the CIF1 struct
				if (pkt->warnings.cif3_warnings == NULL)
				{
					pkt->warnings.cif3_warnings = malloc(sizeof(struct vita49_2_cif3_fields));
					if (pkt->warnings.cif3_warnings == NULL)
						return -ENOMEM;
				}

				pkt->warnings.cif3_warnings->word = htonl(buf[buffer_index++]);
			}

			// CIF7 Word
			if (pkt->warnings.cif0_warnings.cif7_enable && pkt->warnings.cif7_warnings != NULL)
			{
				if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
					return -EINVAL;

				// Checking if memory needs to be allocated for the CIF1 struct
				if (pkt->warnings.cif7_warnings == NULL)
				{
					pkt->warnings.cif7_warnings = malloc(sizeof(struct vita49_2_cif7_fields));
					if (pkt->warnings.cif7_warnings == NULL)
						return -ENOMEM;
				}

				pkt->warnings.cif7_warnings->word = htonl(buf[buffer_index++]);
			}
		}

		// =========================================================
		// ERROR INDICATOR FIELDS
		// =========================================================
		
		if (pkt->command_prologue.ack_cam->errors_present)
		{
			// CIF0 Word
			if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
				return -EINVAL;

			pkt->errors.cif0_errors.word = htonl(buf[buffer_index++]);

			// CIF1 Word
			if (pkt->errors.cif0_errors.cif1_enable && pkt->errors.cif1_errors != NULL)
			{
				if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
					return -EINVAL;

				// Checking if memory needs to be allocated for the CIF1 struct
				if (pkt->errors.cif1_errors == NULL)
				{
					pkt->errors.cif1_errors = malloc(sizeof(struct vita49_2_cif1_fields));
					if (pkt->errors.cif1_errors == NULL)
						return -ENOMEM;
				}

				pkt->errors.cif1_errors->word = htonl(buf[buffer_index++]);
			}

			// CIF2 Word
			if (pkt->errors.cif0_errors.cif2_enable && pkt->errors.cif2_errors != NULL)
			{
				if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
					return -EINVAL;

				// Checking if memory needs to be allocated for the CIF1 struct
				if (pkt->errors.cif2_errors == NULL)
				{
					pkt->errors.cif2_errors = malloc(sizeof(struct vita49_2_cif2_fields));
					if (pkt->errors.cif2_errors == NULL)
						return -ENOMEM;
				}

				pkt->errors.cif2_errors->word = htonl(buf[buffer_index++]);
			}

			// CIF3 Word
			if (pkt->errors.cif0_errors.cif3_enable && pkt->errors.cif3_errors != NULL)
			{
				if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
					return -EINVAL;

				// Checking if memory needs to be allocated for the CIF1 struct
				if (pkt->errors.cif3_errors == NULL)
				{
					pkt->errors.cif3_errors = malloc(sizeof(struct vita49_2_cif3_fields));
					if (pkt->errors.cif3_errors == NULL)
						return -ENOMEM;
				}

				pkt->errors.cif3_errors->word = htonl(buf[buffer_index++]);
			}

			// CIF7 Word
			if (pkt->errors.cif0_errors.cif7_enable && pkt->errors.cif7_errors != NULL)
			{
				if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
					return -EINVAL;

				// Checking if memory needs to be allocated for the CIF1 struct
				if (pkt->errors.cif7_errors == NULL)
				{
					pkt->errors.cif7_errors = malloc(sizeof(struct vita49_2_cif7_fields));
					if (pkt->errors.cif7_errors == NULL)
						return -ENOMEM;
				}

				pkt->errors.cif7_errors->word = htonl(buf[buffer_index++]);
			}
		}

		// =========================================================
		// WARNING FIELDS
		// =========================================================
		if (pkt->command_prologue.ack_cam->warnings_present)
		{
			// CIF0 Attributes
			if ((ret_value = vita49_2_parse_cif0_attributes(pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, buf, buffer_index, &pkt->warnings.cif0_warnings)) < 0)
				return ret_value;

			buffer_index += ret_value;

			// CIF1 Attributes
			// TODO: Parse the CIF 1 fields and populate the CIF 1 warnings struct

			// CIF2 Attributes
			// TODO: Parse the CIF 2 fields and populate the CIF 2 warnings struct

			// CIF3 Attributes
			// TODO: Parse the CIF 3 fields and populate the CIF 3 warnings struct

			// CIF7 Attributes
			// TODO: Parse the CIF 7 fields and populate the CIF 7 warnings struct
		}

		// =========================================================
		// ERROR FIELDS
		// =========================================================
		if (pkt->command_prologue.ack_cam->errors_present)
		{
			// CIF0 Attributes
			if ((ret_value = vita49_2_parse_cif0_attributes(pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, buf, buffer_index, &pkt->errors.cif0_errors)) < 0)
				return ret_value;

			buffer_index += ret_value;

			// CIF1 Attributes
			// TODO: Parse the CIF 1 fields and populate the CIF 1 warnings struct

			// CIF2 Attributes
			// TODO: Parse the CIF 2 fields and populate the CIF 2 warnings struct

			// CIF3 Attributes
			// TODO: Parse the CIF 3 fields and populate the CIF 3 warnings struct

			// CIF7 Attributes
			// TODO: Parse the CIF 7 fields and populate the CIF 7 warnings struct
		}
	}

	return 0;
}

__vrt_api ssize_t vita49_2_serialize_ackV_packet(struct vita49_2_ackV_packet* const pkt, uint32_t* const buf, size_t max_words)
{
	if (pkt == NULL || buf == NULL || max_words == 0)
		return -EINVAL;

	if (pkt->command_prologue.ack_cam == NULL)
		return -EINVAL;

	ssize_t buffer_index;

	// Command Prologue
	if ((buffer_index = vita49_2_serialize_command_prologue(&pkt->command_prologue, buf, max_words, false)) < 0)
		return buffer_index;

	// Payload (Warning/Error Indicator Fields + Warning/Error Indicators)
	{
	
		// The Payload is structured as follows:
			// Warning Indicator Fields:
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
		
		if (pkt->command_prologue.ack_cam->warnings_present)
		{
			// =========================================================
			// WARNING INDICATOR FIELDS
			// =========================================================

			// CIF0 Word

			if (buffer_index >= max_words)
				return -ENOBUFS;

			buf[buffer_index++] = htonl(pkt->warnings.cif0_warnings.word);

			// CIF1 Word
			if (pkt->warnings.cif0_warnings.cif1_enable && pkt->warnings.cif1_warnings != NULL)
			{
				if (buffer_index >= max_words)
					return -ENOBUFS;

				buf[buffer_index++] = htonl(pkt->warnings.cif1_warnings->word);
			}

			// CIF2 Word
			if (pkt->warnings.cif0_warnings.cif2_enable && pkt->warnings.cif2_warnings != NULL)
			{
				if (buffer_index >= max_words)
					return -ENOBUFS;

				buf[buffer_index++] = htonl(pkt->warnings.cif2_warnings->word);
			}

			// CIF3 Word
			if (pkt->warnings.cif0_warnings.cif3_enable && pkt->warnings.cif3_warnings != NULL)
			{
				if (buffer_index >= max_words)
					return -ENOBUFS;

				buf[buffer_index++] = htonl(pkt->warnings.cif3_warnings->word);
			}

			// CIF7 Word
			if (pkt->warnings.cif0_warnings.cif7_enable && pkt->warnings.cif7_warnings != NULL)
			{
				if (buffer_index >= max_words)
					return -ENOBUFS;

				buf[buffer_index++] = htonl(pkt->warnings.cif7_warnings->word);
			}

			// =========================================================
			// WARNING FIELDS
			// =========================================================

			// CIF0

			// Iterating through the buffer of indicator words and copying them over (also handling byte order translation)
			for (uint16_t i = 0; i < pkt->warnings.warnings_payload_num_words; i++)
			{
				// If the warning indicator is set to 0, that means we cleared it while writing the error indicators at some point,
				// thus this indicator shouldn't be written
				if (pkt->warnings.warnings_payload[i].word != 0)
					buf[buffer_index++] = htonl(pkt->warnings.warnings_payload[i].word);
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
	}

	// Update packet size in header
	pkt->command_prologue.common_prologue.header.packet_size_words = buffer_index;
	buf[0] = htonl(pkt->command_prologue.common_prologue.header.word);

	return buffer_index;
}

__vrt_api int vita49_2_parse_ackV_packet(const uint32_t* const buf, size_t buf_words, struct vita49_2_ackV_packet* const pkt)
{
	if (buf == NULL || pkt == NULL || buf_words == 0)
		return -EINVAL;

	memset(pkt, 0, sizeof(*pkt));
	ssize_t buffer_index, ret_value;

	// Command Prologue
	if ((buffer_index = vita49_2_parse_command_prologue(buf, buf_words, &pkt->command_prologue, VITA49_2_PKT_TYPE_COMMAND)) < 0)
		return buffer_index;

	// Payload (CIF0/1/2/3/7 and attribute values)
	{
		// The Payload is structured as follows:
			// Warning Indicator Fields:
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

		if (pkt->command_prologue.ack_cam->warnings_present)
		{
			// =========================================================
			// WARNING INDICATOR FIELDS
			// =========================================================
			
			// CIF0 Word
			if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
				return -EINVAL;

			pkt->warnings.cif0_warnings.word = htonl(buf[buffer_index++]);

			// CIF1 Word
			if (pkt->warnings.cif0_warnings.cif1_enable && pkt->warnings.cif1_warnings != NULL)
			{
				if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
					return -EINVAL;

				// Checking if memory needs to be allocated for the CIF1 struct
				if (pkt->warnings.cif1_warnings == NULL)
				{
					pkt->warnings.cif1_warnings = malloc(sizeof(struct vita49_2_cif1_fields));
					if (pkt->warnings.cif1_warnings == NULL)
						return -ENOMEM;
				}

				pkt->warnings.cif1_warnings->word = htonl(buf[buffer_index++]);
			}

			// CIF2 Word
			if (pkt->warnings.cif0_warnings.cif2_enable && pkt->warnings.cif2_warnings != NULL)
			{
				if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
					return -EINVAL;

				// Checking if memory needs to be allocated for the CIF1 struct
				if (pkt->warnings.cif2_warnings == NULL)
				{
					pkt->warnings.cif2_warnings = malloc(sizeof(struct vita49_2_cif2_fields));
					if (pkt->warnings.cif2_warnings == NULL)
						return -ENOMEM;
				}

				pkt->warnings.cif2_warnings->word = htonl(buf[buffer_index++]);
			}

			// CIF3 Word
			if (pkt->warnings.cif0_warnings.cif3_enable && pkt->warnings.cif3_warnings != NULL)
			{
				if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
					return -EINVAL;

				// Checking if memory needs to be allocated for the CIF1 struct
				if (pkt->warnings.cif3_warnings == NULL)
				{
					pkt->warnings.cif3_warnings = malloc(sizeof(struct vita49_2_cif3_fields));
					if (pkt->warnings.cif3_warnings == NULL)
						return -ENOMEM;
				}

				pkt->warnings.cif3_warnings->word = htonl(buf[buffer_index++]);
			}

			// CIF7 Word
			if (pkt->warnings.cif0_warnings.cif7_enable && pkt->warnings.cif7_warnings != NULL)
			{
				if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words) 
					return -EINVAL;

				// Checking if memory needs to be allocated for the CIF1 struct
				if (pkt->warnings.cif7_warnings == NULL)
				{
					pkt->warnings.cif7_warnings = malloc(sizeof(struct vita49_2_cif7_fields));
					if (pkt->warnings.cif7_warnings == NULL)
						return -ENOMEM;
				}

				pkt->warnings.cif7_warnings->word = htonl(buf[buffer_index++]);
			}

			// =========================================================
			// WARNING FIELDS
			// =========================================================

			// CIF0 Attributes
			if ((ret_value = vita49_2_parse_cif0_attributes(pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, buf, buffer_index, &pkt->warnings.cif0_warnings)) < 0)
				return ret_value;

			buffer_index += ret_value;

			// CIF1 Attributes
			// TODO: Parse the CIF 1 fields and populate the CIF 1 warnings struct

			// CIF2 Attributes
			// TODO: Parse the CIF 2 fields and populate the CIF 2 warnings struct

			// CIF3 Attributes
			// TODO: Parse the CIF 3 fields and populate the CIF 3 warnings struct

			// CIF7 Attributes
			// TODO: Parse the CIF 7 fields and populate the CIF 7 warnings struct
		}
	}

	return 0;
}

__vrt_api ssize_t vita49_2_serialize_ackS_packet(struct vita49_2_ackS_packet* const pkt, uint32_t* const buf, size_t max_words)
{
	if (pkt == NULL || buf == NULL || max_words == 0)
		return -EINVAL;

	if (pkt->command_prologue.ack_cam == NULL)
		return -EINVAL;

	ssize_t buffer_index, ret_value;

	// Command Prologue
	if ((buffer_index = vita49_2_serialize_command_prologue(&pkt->command_prologue, buf, max_words, false)) < 0)
		return buffer_index;

	// Payload (CIF Words + CIF Attribute Data)
	if ((ret_value = vita49_2_serialize_cif_fields(&pkt->cif0, pkt->cif1, pkt->cif2, pkt->cif3, pkt->cif7, buf, buffer_index, max_words, true)) < 0)
		return ret_value;

	buffer_index += ret_value;

	// Update packet size in header
	pkt->command_prologue.common_prologue.header.packet_size_words = buffer_index;
	buf[0] = htonl(pkt->command_prologue.common_prologue.header.word);

	return buffer_index;
}

__vrt_api int vita49_2_parse_ackS_packet(const uint32_t* const buf, size_t buf_words, struct vita49_2_ackS_packet* const pkt)
{
	if (buf == NULL || pkt == NULL || buf_words == 0)
		return -EINVAL;

	memset(pkt, 0, sizeof(*pkt));
	ssize_t buffer_index;

	// Command Prologue
	if ((buffer_index = vita49_2_parse_command_prologue(buf, buf_words, &pkt->command_prologue, VITA49_2_PKT_TYPE_COMMAND)) < 0)
		return buffer_index;

	// Payload (CIF Fields + Attributes)
	if ((buffer_index = vita49_2_parse_cif_fields(pkt->command_prologue.common_prologue.header.packet_size_words - buffer_index, buf, buffer_index, &pkt->cif0, pkt->cif1, pkt->cif2, pkt->cif3, pkt->cif7)) < 0)
		return buffer_index;
	
	return 0;
}

__vrt_api ssize_t vita49_2_serialize_control_extension_packet(struct vita49_2_control_extension_packet* const pkt, uint32_t* const buf, size_t max_words)
{
	if (pkt == NULL || buf == NULL || max_words == 0)
		return -EINVAL;

	if (pkt->command_prologue.control_cam == NULL)
		return -EINVAL;

	ssize_t buffer_index;

	// Command Prologue
	if ((buffer_index = vita49_2_serialize_command_prologue(&pkt->command_prologue, buf, max_words, true)) < 0)
		return buffer_index;

	/* Payload of Control Extension Words */
	size_t payload_start = buffer_index;

	for (struct vita49_2_control_extension_word_node* node = pkt->payload; node != NULL; node = node->next)
	{
		if (buffer_index >= max_words)
		{
			fprintf(stderr, "vita49_2_process: Not enough buffer space for Control Extension Packet payload.\n");
			return -ENOBUFS;
		}

		buf[buffer_index++] = htonl(node->control_extension.word);

		// There's 2 payload formats/structures that ADI uses for the Control Extension Packets: Implicit and Explicit
		// See the definition of the vita49_2_control_extension_description struct in vita49_2_packet_elements.h for more information
			
		if (pkt->command_prologue.common_prologue.class_id.packet_class_code == VITA49_2_PKT_CLASS_CTRL_EXT_IMPLICIT)
		{
			// We also need to copy the new attribute value to the buffer
			switch (node->control_extension.implicit.data_type)
			{
				case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_LL:
					
					if ((buffer_index + 1) >= max_words)
						return -ENOBUFS;

					uint64_t u64;
					memcpy(&u64, &node->data, sizeof(u64));
					buf[buffer_index++] = htonl((uint32_t)(u64 >> 32));
					buf[buffer_index++] = htonl((uint32_t)(u64 & 0xFFFFFFFF));

					break;

				case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_F:

					if (buffer_index >= max_words)
						return -EINVAL;

					if (node->control_extension.implicit.encoding == VITA49_2_CONTROL_EXTENSION_ENCODING_NONE)
					{
						buf[buffer_index++] = htonl(node->data.u32);
					}
					else if (node->control_extension.implicit.encoding == VITA49_2_CONTROL_EXTENSION_ENCODING_9_7)
					{
						buf[buffer_index++] = htonl((uint32_t)(convert_to_9_7(node->data.f)));
					}
					else if (node->control_extension.implicit.encoding == VITA49_2_CONTROL_EXTENSION_ENCODING_10_6)
					{
						buf[buffer_index++] = htonl((uint32_t)(convert_to_10_6(node->data.f)));
					}
					else
					{
						fprintf(stderr, "vita49_2_process: Invalid encoding type for float while trying to generate a Control Extension Packet: %d\n", node->control_extension.implicit.encoding);
						return -EINVAL;
					}

				case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_D:

					if ((buffer_index + 1)>= max_words)
						return -EINVAL;
					
					if (node->control_extension.implicit.encoding == VITA49_2_CONTROL_EXTENSION_ENCODING_NONE)
					{
						uint64_t u64;
						memcpy(&u64, &node->data, sizeof(u64));
						buf[buffer_index++] = htonl((uint32_t)(u64 >> 32));
						buf[buffer_index++] = htonl((uint32_t)(u64 & 0xFFFFFFFF));
					}
					else if (node->control_extension.implicit.encoding == VITA49_2_CONTROL_EXTENSION_ENCODING_44_20)
					{
						int64_t converted = convert_to_44_20(node->data.d);
						buf[buffer_index++] = htonl((uint32_t)(converted >> 32));
						buf[buffer_index++] = htonl((uint32_t)(converted));
					}
					else
					{
						fprintf(stderr, "vita49_2_process: Invalid encoding type for double in Control Extension Packet: %d\n", node->control_extension.implicit.encoding);
						return -EINVAL;
					}

					break;

				case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_B:
					
					if (buffer_index >= max_words)
						return -EINVAL;

					buf[buffer_index++] = htonl(node->data.u32);
					break;

				case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_S:

					// For Implicit types, do nothing since this will be handled by the "options" field
					break;

				default:
					fprintf(stderr, "vita49_2_process: Unrecognized data type encoding while attempting to serialize a Control Extension Packet: %d\n", node->control_extension.implicit.data_type);
					return -EINVAL;
			}
		}
		else if (pkt->command_prologue.common_prologue.class_id.packet_class_code == VITA49_2_PKT_CLASS_CTRL_EXT_EXPLICIT)
		{
			// The attribute metadata (device name, channel name, attribute name) needs to be copied to the buffer along with the
			// data itself.

			// String length in terms of 32-bit words (ceiling division)
			uint32_t string_lengths = (
										node->control_extension.explicit.device_name_length +
										node->control_extension.explicit.channel_name_length + 
										node->control_extension.explicit.attribute_name_length +
										4 - 1) / 4;

			if ((buffer_index + string_lengths) >= max_words)
			{
				fprintf(stderr, "vita49_2_process: Not enough buffer space to populate attribute metadata.\n");
				return -ENOBUFS;
			}

			memcpy(&buf[buffer_index], node->device_name, node->control_extension.explicit.device_name_length);
			buffer_index += (node->control_extension.explicit.device_name_length + 4 - 1)/4;

			memcpy(&buf[buffer_index], node->channel_name, node->control_extension.explicit.channel_name_length);
			buffer_index += (node->control_extension.explicit.channel_name_length + 4 - 1)/4;

			memcpy(&buf[buffer_index], node->attribute_name, node->control_extension.explicit.attribute_name_length);
			buffer_index += (node->control_extension.explicit.attribute_name_length + 4 - 1)/4;

			switch (node->control_extension.explicit.data_type)
			{
				case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_LL:
					
					if ((buffer_index + 1) >= max_words)
						return -ENOBUFS;

					uint64_t u64;
					memcpy(&u64, &node->data, sizeof(u64));
					buf[buffer_index++] = htonl((uint32_t)(u64 >> 32));
					buf[buffer_index++] = htonl((uint32_t)(u64 & 0xFFFFFFFF));

					break;

				case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_F:

					if (buffer_index >= max_words)
						return -EINVAL;

					if (node->control_extension.explicit.encoding == VITA49_2_CONTROL_EXTENSION_ENCODING_NONE)
					{
						buf[buffer_index++] = htonl(node->data.u32);
					}
					else if (node->control_extension.explicit.encoding == VITA49_2_CONTROL_EXTENSION_ENCODING_9_7)
					{
						buf[buffer_index++] = htonl((uint32_t)(convert_to_9_7(node->data.f)));
					}
					else if (node->control_extension.explicit.encoding == VITA49_2_CONTROL_EXTENSION_ENCODING_10_6)
					{
						buf[buffer_index++] = htonl((uint32_t)(convert_to_10_6(node->data.f)));
					}
					else
					{
						fprintf(stderr, "vita49_2_process: Invalid encoding type for float while trying to generate a Control Extension Packet: %d\n", node->control_extension.explicit.encoding);
						return -EINVAL;
					}

				case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_D:

					if ((buffer_index + 1)>= max_words)
						return -EINVAL;
					
					if (node->control_extension.explicit.encoding == VITA49_2_CONTROL_EXTENSION_ENCODING_NONE)
					{
						uint64_t u64;
						memcpy(&u64, &node->data, sizeof(u64));
						buf[buffer_index++] = htonl((uint32_t)(u64 >> 32));
						buf[buffer_index++] = htonl((uint32_t)(u64 & 0xFFFFFFFF));
					}
					else if (node->control_extension.explicit.encoding == VITA49_2_CONTROL_EXTENSION_ENCODING_44_20)
					{
						int64_t converted = convert_to_44_20(node->data.d);
						buf[buffer_index++] = htonl((uint32_t)(converted >> 32));
						buf[buffer_index++] = htonl((uint32_t)(converted));
					}
					else
					{
						fprintf(stderr, "vita49_2_process: Invalid encoding type for double in Control Extension Packet: %d\n", node->control_extension.explicit.encoding);
						return -EINVAL;
					}

					break;

				case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_B:
					
					if (buffer_index >= max_words)
						return -EINVAL;

					buf[buffer_index++] = htonl(node->data.u32);
					break;

				case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_S:

					// For Explicit types, we have to copy the string data into the buffer
					if (pkt->command_prologue.common_prologue.class_id.packet_class_code == VITA49_2_PKT_CLASS_CTRL_EXT_EXPLICIT)
					{
						if ((buffer_index + (node->control_extension.explicit.data_length + 4 - 1)/4) >= max_words)
						{
							fprintf(stderr, "vita49_2_process: Not enough buffer space for string data while generating Control Extension Packet.\n");
							return -ENOBUFS;
						}

						memcpy(&buf[buffer_index], node->string_data, node->control_extension.explicit.data_length);
						buffer_index += (node->control_extension.explicit.data_length + 4 - 1)/4;
					}

					break;

				default:
					fprintf(stderr, "vita49_2_process: Unrecognized data type encoding while attempting to serialize a Control Extension Packet: %d\n", node->control_extension.explicit.data_type);
					return -EINVAL;
			}
		}
		else
		{
			fprintf(stderr, "vita49_2_process: Unrecognized packet class code while generating Control Extension Packet: %d\n", pkt->command_prologue.common_prologue.class_id.packet_class_code);
			return -EINVAL;
		}
	}

	// Update packet size in header
	pkt->command_prologue.common_prologue.header.packet_size_words = buffer_index;
	buf[0] = htonl(pkt->command_prologue.common_prologue.header.word);

	return buffer_index;
}

__vrt_api int vita49_2_parse_control_extension_packet(const uint32_t* const buf, size_t buf_words, struct vita49_2_control_extension_packet* const pkt)
{
	if (buf == NULL || pkt == NULL || buf_words == 0)
		return -EINVAL;

	memset(pkt, 0, sizeof(*pkt));
	ssize_t buffer_index, ret_value;

	// Command Prologue
	if ((buffer_index = vita49_2_parse_command_prologue(buf, buf_words, &pkt->command_prologue, VITA49_2_PKT_TYPE_COMMAND)) < 0)
		return buffer_index;


	// Payload of Control Extension Words

	// Setting up the head node
	struct vita49_2_control_extension_word_node* current_node;
	current_node = calloc(1, sizeof(struct vita49_2_control_extension_word_node));
	
	if (current_node == NULL)
	{
		fprintf(stderr, "vita49_2_process: Failed to allocate memory while parsing Control Extension Packet.\n");
		return -ENOMEM;
	}

	pkt->payload = current_node;

	// There's 2 payload formats/structures that ADI uses for the Control Extension Packets: Implicit and Explicit
	// See the definition of the vita49_2_control_extension_description struct in vita49_2_packet_elements.h for more information

	if (pkt->command_prologue.common_prologue.class_id.packet_class_code == VITA49_2_PKT_CLASS_CTRL_EXT_IMPLICIT)
	{
		while (buffer_index < pkt->command_prologue.common_prologue.header.packet_size_words)
		{
			// Avoid reallocating memory for the head node
			if (current_node == NULL)
			{
				current_node = calloc(1, sizeof(struct vita49_2_control_extension_word_node));

				if (current_node == NULL)
				{
					fprintf(stderr, "vita49_2_process: Failed to allocate memory while parsing Control Extension Packet.\n");
					return -ENOMEM;
				}
			}

			current_node->control_extension.word = ntohl(buf[buffer_index++]);

			// Now we have to extract the data associated with this field as well
			switch (current_node->control_extension.implicit.data_type)
			{
				case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_LL:

					if ((buffer_index + 1) >= pkt->command_prologue.common_prologue.header.packet_size_words)
						return -EINVAL;

					uint32_t upper = ntohl(buf[buffer_index++]);
					uint32_t lower = ntohl(buf[buffer_index++]);
					uint64_t comb = (((uint64_t)(upper) << 32) | (lower));
					memcpy(&current_node->data, &comb, sizeof(long long));
					
					break;

				case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_F:

					if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words)
						return -EINVAL;

					if (current_node->control_extension.implicit.encoding == VITA49_2_CONTROL_EXTENSION_ENCODING_NONE)
					{
						current_node->data.u32 = ntohl(buf[buffer_index++]);
					}
					else if (current_node->control_extension.implicit.encoding == VITA49_2_CONTROL_EXTENSION_ENCODING_9_7)
					{
						uint32_t word = ntohl(buf[buffer_index++]);
						current_node->data.f = convert_from_9_7(word);
					}
					else if (current_node->control_extension.implicit.encoding == VITA49_2_CONTROL_EXTENSION_ENCODING_10_6)
					{
						uint32_t word = ntohl(buf[buffer_index++]);
						current_node->data.f = convert_from_10_6(word);
					}
					else
					{
						fprintf(stderr, "vita49_2_process: Invalid encoding type for float in Control Extension Packet: %d\n", current_node->control_extension.implicit.encoding);
						return -EINVAL;
					}
					
					break;

				case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_D:
				
					if ((buffer_index + 1) >= pkt->command_prologue.common_prologue.header.packet_size_words)
						return -EINVAL;
					
					if (current_node->control_extension.implicit.encoding == VITA49_2_CONTROL_EXTENSION_ENCODING_NONE)
					{
						uint32_t upper = ntohl(buf[buffer_index++]);
						uint32_t lower = ntohl(buf[buffer_index++]);
						uint64_t comb = (((uint64_t)(upper) << 32) | (lower));
						memcpy(&current_node->data, &comb, sizeof(comb));
					}
					else if (current_node->control_extension.implicit.encoding == VITA49_2_CONTROL_EXTENSION_ENCODING_44_20)
					{
						uint32_t upper = ntohl(buf[buffer_index++]);
						uint32_t lower = ntohl(buf[buffer_index++]);
						uint64_t comb = (((uint64_t)(upper) << 32) | (lower));
						int64_t comb_s;
						memcpy(&comb_s, &comb, sizeof(comb));

						current_node->data.d = convert_from_44_20(comb_s);
					}
					else
					{
						fprintf(stderr, "vita49_2_process: Invalid encoding type for double in Control Extension Packet: %d\n", current_node->control_extension.implicit.encoding);
						return -EINVAL;
					}

					break;

				case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_B:
				
					if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words)
						return -EINVAL;

					current_node->data.u32 = ntohl(buf[buffer_index++]);
					break;

				case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_S:

					// Nothing has to be done since we'll use the "<attribute name>_available" fd for setting the value of this attribute
					break;

				default:
					fprintf(stderr, "vita49_2_process: Unrecognized data type encoding while parsing Control Extension Packet: %d\n", current_node->control_extension.implicit.data_type);
					return -EINVAL;
			}

			current_node = &current_node->next;
		}
	}
	else if (pkt->command_prologue.common_prologue.class_id.packet_class_code == VITA49_2_PKT_CLASS_CTRL_EXT_EXPLICIT)
	{
		if (pkt->command_prologue.common_prologue.class_id.pad_bit_count % 8 != 0)
		{
			fprintf(stderr, "vita49_2_process: Invalid pad bit count in Control Extension Packet. Must be divisible by 8 to be parsed properly, instead received: %d\n", pkt->command_prologue.common_prologue.class_id.pad_bit_count);
			return -EINVAL;
		}
		
		while (buffer_index < pkt->command_prologue.common_prologue.header.packet_size_words)
		{
			// Avoid reallocating memory for the head node
			if (current_node == NULL)
			{
				current_node = calloc(1, sizeof(struct vita49_2_control_extension_word_node));

				if (current_node == NULL)
				{
					fprintf(stderr, "vita49_2_process: Failed to allocate memory while parsing Control Extension Packet.\n");
					return -ENOMEM;
				}
			}

			current_node->control_extension.word = ntohl(buf[buffer_index++]);

			// Extracting the attribute metadata (device name, channel name, attribute name) in terms of 32-bit words (ceiling division)
			uint32_t string_lengths = (
										current_node->control_extension.explicit.device_name_length +
										current_node->control_extension.explicit.channel_name_length + 
										current_node->control_extension.explicit.attribute_name_length +
										4 - 1) / 4;

			if ((buffer_index + string_lengths) >= pkt->command_prologue.common_prologue.header.packet_size_words)
			{
				fprintf(stderr, "vita49_2_process: Not enough buffer space to parse attribute metadata.\n");
				return -ENOBUFS;
			}

			// Device Name
			current_node->device_name = malloc(current_node->control_extension.explicit.device_name_length + 1);
			if (current_node->device_name == NULL)
			{
				fprintf(stderr, "vita49_2_process: Failed to allocate memory for device name while parsing Control Extension Packet.\n");
				return -ENOMEM;
			}
			memcpy(current_node->device_name, &buf[buffer_index], current_node->control_extension.explicit.device_name_length);
			current_node->device_name[current_node->control_extension.explicit.device_name_length] = '\0';
			buffer_index += (current_node->control_extension.explicit.device_name_length + 4 - 1)/4;

			// Channel Name
			current_node->channel_name = malloc(current_node->control_extension.explicit.channel_name_length + 1);
			if (current_node->channel_name == NULL)
			{
				fprintf(stderr, "vita49_2_process: Failed to allocate memory for channel name while parsing Control Extension Packet.\n");
				return -ENOMEM;
			}
			memcpy(current_node->channel_name, &buf[buffer_index], current_node->control_extension.explicit.channel_name_length);
			current_node->channel_name[current_node->control_extension.explicit.channel_name_length] = '\0';
			buffer_index += (current_node->control_extension.explicit.channel_name_length + 4 - 1)/4;

			// Attribute Name
			current_node->attribute_name = malloc(current_node->control_extension.explicit.attribute_name_length + 1);
			if (current_node->attribute_name == NULL)
			{
				fprintf(stderr, "vita49_2_process: Failed to allocate memory for attribute name while parsing Control Extension Packet.\n");
				return -ENOMEM;
			}
			memcpy(current_node->attribute_name, &buf[buffer_index], current_node->control_extension.explicit.attribute_name_length);
			current_node->attribute_name[current_node->control_extension.explicit.attribute_name_length] = '\0';
			buffer_index += (current_node->control_extension.explicit.attribute_name_length + 4 - 1)/4;

			// Extracting the data
			switch (current_node->control_extension.explicit.data_type)
			{
				case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_LL:

					if ((buffer_index + 1) >= pkt->command_prologue.common_prologue.header.packet_size_words)
						return -EINVAL;

					uint32_t upper = ntohl(buf[buffer_index++]);
					uint32_t lower = ntohl(buf[buffer_index++]);
					uint64_t comb = (((uint64_t)(upper) << 32) | (lower));
					memcpy(&current_node->data, &comb, sizeof(long long));
					
					break;

				case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_F:

					if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words)
						return -EINVAL;

					if (current_node->control_extension.implicit.encoding == VITA49_2_CONTROL_EXTENSION_ENCODING_NONE)
					{
						current_node->data.u32 = ntohl(buf[buffer_index++]);
					}
					else if (current_node->control_extension.implicit.encoding == VITA49_2_CONTROL_EXTENSION_ENCODING_9_7)
					{
						uint32_t word = ntohl(buf[buffer_index++]);
						current_node->data.f = convert_from_9_7(word);
					}
					else if (current_node->control_extension.implicit.encoding == VITA49_2_CONTROL_EXTENSION_ENCODING_10_6)
					{
						uint32_t word = ntohl(buf[buffer_index++]);
						current_node->data.f = convert_from_10_6(word);
					}
					else
					{
						fprintf(stderr, "vita49_2_process: Invalid encoding type for float in Control Extension Packet: %d\n", current_node->control_extension.implicit.encoding);
						return -EINVAL;
					}
					
					break;

				case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_D:
				
					if ((buffer_index + 1) >= pkt->command_prologue.common_prologue.header.packet_size_words)
						return -EINVAL;
					
					if (current_node->control_extension.implicit.encoding == VITA49_2_CONTROL_EXTENSION_ENCODING_NONE)
					{
						uint32_t upper = ntohl(buf[buffer_index++]);
						uint32_t lower = ntohl(buf[buffer_index++]);
						uint64_t comb = (((uint64_t)(upper) << 32) | (lower));
						memcpy(&current_node->data, &comb, sizeof(comb));
					}
					else if (current_node->control_extension.implicit.encoding == VITA49_2_CONTROL_EXTENSION_ENCODING_44_20)
					{
						uint32_t upper = ntohl(buf[buffer_index++]);
						uint32_t lower = ntohl(buf[buffer_index++]);
						uint64_t comb = (((uint64_t)(upper) << 32) | (lower));
						int64_t comb_s;
						memcpy(&comb_s, &comb, sizeof(comb));

						current_node->data.d = convert_from_44_20(comb_s);
					}
					else
					{
						fprintf(stderr, "vita49_2_process: Invalid encoding type for double in Control Extension Packet: %d\n", current_node->control_extension.implicit.encoding);
						return -EINVAL;
					}

					break;

				case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_B:
				
					if (buffer_index >= pkt->command_prologue.common_prologue.header.packet_size_words)
						return -EINVAL;

					current_node->data.u32 = ntohl(buf[buffer_index++]);
					break;

				case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_S:

					if (current_node->control_extension.explicit.data_length == 0)
					{
						fprintf(stderr, "vita49_2_process: Encountered String Type data with length of 0 while parsing Control Extension Packet.\n");
						return -EINVAL;
					}

					if (((buffer_index + current_node->control_extension.explicit.data_length + 4 - 1)/4) >= pkt->command_prologue.common_prologue.header.packet_size_words)
					{
						fprintf(stderr, "vita49_2_process: Not enough buffer space to parse data from Control Extension Packet.\n");
						return -ENOBUFS;
					}

					current_node->string_data = malloc(current_node->control_extension.explicit.data_length + 1);
					if (current_node->string_data == NULL)
					{
						fprintf(stderr, "vita49_2_process: Failed to allocate memory for the new attribute data while parsing Control Extension Packet.\n");
						return -ENOMEM;
					}

					memcpy(current_node->string_data, &buf[buffer_index], current_node->control_extension.explicit.data_length);
					current_node->string_data[current_node->control_extension.explicit.data_length] = '\0';
					buffer_index += (current_node->control_extension.explicit.data_length + 4 - 1)/4;

					break;

				default:
					fprintf(stderr, "vita49_2_process: Unrecognized data type encoding while parsing Control Extension Packet: %d\n", current_node->control_extension.implicit.data_type);
					return -EINVAL;
			}

			current_node = &current_node->next;
		}
	}
	else
	{
		fprintf(stderr, "vita49_2_process: Unrecognized packet class code while parsing Control Extension Packet: %d\n", pkt->command_prologue.common_prologue.class_id.packet_class_code);
		free(pkt->payload);
		pkt->payload = NULL;
		return -EINVAL;
	}

	return 0;
}