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

#ifndef __VITA49_2_PACKET_TYPES_H__
#define __VITA49_2_PACKET_TYPES_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include "vita49_2_packet_elements.h"

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

/**
 * @struct vita49_2_data_packet
 * @brief Represents a parsed VITA 49.2 Signal Data Packet.
 *
 * This structure holds all decompressed fields and metadata of a 
 * VITA 49.2 Signal Data Packet, providing easy access to components.
 */
struct vita49_2_data_packet {
	
	struct vita49_2_prologue prologue;	/* Common fields present in every VITA 49.2 packet */
	
	const struct vita49_2_iq_item *payload;  /* Pointer to the start of the payload words */
	uint16_t payload_num_words;     	/* Number of 32-bit words in the payload */
	
	struct vita49_2_trailer trailer; 	/* Optional 32-bit trailer */
	bool has_trailer;         			/* True if trailer is populated */

};

/**
 * @struct vita49_2_context_packet
 * @brief Represents a parsed VITA 49.2 Context Packet.
 * 
 * This structure holds all decompressed fields and metadata of a 
 * VITA 49.2 Context Packet, providing easy access to components.
 */
struct vita49_2_context_packet {
	
	struct vita49_2_prologue prologue;	/* Common fields present in every VITA 49.2 packet */
	
	struct vita49_2_cif0_fields cif0;	/* Context Indicator Fields 0 */

	// Unlikely to be used, but ADI retains the right to implement these fields in the future.
	// Using pointers as embedding each of these structs would result in a lot of bloat.
	struct vita49_2_cif7_fields* cif7;	/* CIF7 Word and Fields */
	struct vita49_2_cif3_fields* cif3;	/* CIF3 Word and Fields */
	struct vita49_2_cif2_fields* cif2;	/* CIF2 Word and Fields */
	struct vita49_2_cif1_fields* cif1;	/* CIF1 Word and Fields */

	// NOTE: The enable bits for CIF1-7 are contained within the cif0_word parameter in the cif0 struct.
	// DO NOT duplicate those enable bits here so we can avoid mismatching states. 
	// Always access those enables through the cif0 struct.

};

/**
 * @struct vita49_2_control_packet
 * @brief Represents a parsed VITA 49.2 Control Packet.
 * 
 * This structure holds all decompressed fields and metadata of a 
 * VITA 49.2 Control Packet, providing easy access to components.
 */
struct vita49_2_control_packet {
	
	struct vita49_2_command_prologue command_prologue;	/* Common fields present in every VITA 49.2 Command Packet */
	
	struct vita49_2_cif0_fields cif0;	/* Context Indicator Fields 0 */

	// Unlikely to be used, but ADI retains the right to implement these fields in the future.
	// Using pointers as embedding each of these structs would result in a lot of bloat.
	struct vita49_2_cif7_fields* cif7;	/* CIF7 Word and Fields */
	struct vita49_2_cif3_fields* cif3;	/* CIF3 Word and Fields */
	struct vita49_2_cif2_fields* cif2;	/* CIF2 Word and Fields */
	struct vita49_2_cif1_fields* cif1;	/* CIF1 Word and Fields */

	// NOTE: The enable bits for CIF1-7 are contained within the cif0_word parameter in the cif0 struct.
	// DO NOT duplicate those enable bits here so we can avoid mismatching states. 
	// Always access those enables through the cif0 struct.

	// The payload will contain the new attribute values that we want to apply to the device/module
	const uint32_t *payload;  			/* Pointer to the start of the payload words */
	uint16_t payload_num_words;     	/* Number of 32-bit words in the payload */

};

/**
 * @struct vita49_2_ackV_X_packet
 * @brief Represents a parsed VITA 49.2 AckV/X Packet.
 * 
 * This structure holds all decompressed fields and metadata of a 
 * VITA 49.2 AckV/X Packet, providing easy access to components.
 */
struct vita49_2_ackV_X_packet {
	
	struct vita49_2_command_prologue prologue;	/* Common fields present in every VITA 49.2 Command Packet */
	

	/* WARNINGS */
	struct vita49_2_warning_error_indicators warning_indicators;	/* Warnings resulting from executing commands */
	struct vita49_2_cif0_fields cif0_warnings;	/* Indicates which of the CIF0 had warnings */

	// Unlikely to be used, but ADI retains the right to implement these fields in the future.
	// Using pointers as embedding each of these structs would result in a lot of bloat.
	struct vita49_2_cif7_fields* cif7_warnings;	/* CIF7 Word and Fields */
	struct vita49_2_cif3_fields* cif3_warnings;	/* CIF3 Word and Fields */
	struct vita49_2_cif2_fields* cif2_warnings;	/* CIF2 Word and Fields */
	struct vita49_2_cif1_fields* cif1_warnings;	/* CIF1 Word and Fields */

	// NOTE: The enable bits for CIF1-7 are contained within the cif0_word parameter in the cif0_warnings struct.
	// DO NOT duplicate those enable bits here so we can avoid mismatching states. 
	// Always access those enables through the cif0_warnings struct.


	/* ERRORS */
	struct vita49_2_warning_error_indicators error_indicators;		/* Errors resulting from executing commands*/
	struct vita49_2_cif0_fields cif0_errors;	/* Indicates which of the CIF0 had warnings */

	struct vita49_2_cif7_fields* cif7_errors;	/* CIF7 Word and Fields */
	struct vita49_2_cif3_fields* cif3_errors;	/* CIF3 Word and Fields */
	struct vita49_2_cif2_fields* cif2_errors;	/* CIF2 Word and Fields */
	struct vita49_2_cif1_fields* cif1_errors;	/* CIF1 Word and Fields */


	// Attribute values that resulted in errors
	const uint32_t *warnings_payload;  			/* Pointer to the start of the payload words */
	uint16_t warnings_payload_num_words;     	/* Number of 32-bit words in the payload */

	// Attribute values that resulted in errors
	const uint32_t *errors_payload;  			/* Pointer to the start of the payload words */
	uint16_t errors_payload_num_words;     	/* Number of 32-bit words in the payload */

};

/**
 * @struct vita49_2_ackS_packet
 * @brief Represents a parsed VITA 49.2 AckS Packet.
 * 
 * This structure holds all decompressed fields and metadata of a 
 * VITA 49.2 AckS Packet, providing easy access to components.
 */
struct vita49_2_ackS_packet {
	
	struct vita49_2_command_prologue prologue;	/* Common fields present in every VITA 49.2 Command Packet */
	
	struct vita49_2_cif0_fields cif0;	/* Context Indicator Fields 0, indicates which attributes/fields will appear in the payload */

	// Unlikely to be used, but ADI retains the right to implement these fields in the future.
	// Using pointers as embedding each of these structs would result in a lot of bloat.
	struct vita49_2_cif7_fields* cif7;	/* CIF7 Word and Fields */
	struct vita49_2_cif3_fields* cif3;	/* CIF3 Word and Fields */
	struct vita49_2_cif2_fields* cif2;	/* CIF2 Word and Fields */
	struct vita49_2_cif1_fields* cif1;	/* CIF1 Word and Fields */

	// NOTE: The enable bits for CIF1-7 are contained within the cif0_word parameter in the cif0 struct.
	// DO NOT duplicate those enable bits here so we can avoid mismatching states. 
	// Always access those enables through the cif0 struct.

	// The payload will contain the attribute values of the fields defined in CIF0 (as well as CIF1-7 if they're enabled)
	// after the Control Packet was executed.
	const uint32_t *payload;  			/* Pointer to the start of the payload words */
	uint16_t payload_num_words;     	/* Number of 32-bit words in the payload */
};

/**
 * @struct vita49_2_extended_control_packet
 * @brief Represents a parsed VITA 49.2 Command Extension Packet.
 * 
 * This structure holds all decompressed fields and metadata of a 
 * VITA 49.2 Command Extension Packet, providing easy access to components.
 * 
 * The purpose of this Command Extension Packet is to issue commands/controls on ADI devices that can't
 * be well translated to CIF0-7 fields.
 */
struct vita49_2_extended_control_packet {
	
	struct vita49_2_command_prologue prologue;	/* Common fields present in every VITA 49.2 Command Packet */
	
	// The payload will contain "vita49_2_extended_control_item" structs which contain the attribute address
	// and new attribute value that we want to configure on the device
	const uint32_t *payload;  			/* Pointer to the start of the payload words */
	uint16_t payload_num_words;     	/* Number of 32-bit words in the payload */
};


#if defined(_WIN32)
#  if defined(LIBIIO_EXPORTS)
#    define __vrt_api __declspec(dllexport)
#  else
#    define __vrt_api __declspec(dllimport)
#  endif
#elif __GNUC__ >= 4 || defined(__clang__)
#  define __vrt_api __attribute__((visibility ("default")))
#else
#  define __vrt_api
#endif


/**
 * @brief Populates a 32-bit word buffer with data for a Signal Data Packet. 
 * The buffer MUST be large enough to hold the generated packet.
 * Returns the number of words written, or a negative error code on failure.
 * 
 * @param pkt 
 * @param buf 
 * @param max_words 
 * @return ssize_t 
 */
__vrt_api ssize_t vita49_2_generate_data_packet(const struct vita49_2_data_packet *pkt, uint32_t *buf, size_t max_words);

/**
 * @brief Parses a buffer of 32-bit words into a vita49_2_data_packet structure. 
 * Returns 0 on success, or a negative error code (e.g. -EINVAL) on failure.
 * 
 * @param buf 
 * @param words 
 * @param pkt 
 * @return int 
 */
__vrt_api int vita49_2_parse_data_packet(const uint32_t *buf, size_t words, struct vita49_2_data_packet *pkt);

/**
 * @brief Populates a 32-bit word buffer with data for a Context Packet. 
 * The buffer MUST be large enough to hold the generated packet.
 * Returns the number of words written, or a negative error code on failure.
 * 
 * @param pkt 
 * @param buf 
 * @param max_words 
 * @return ssize_t 
 */
__vrt_api ssize_t vita49_2_generate_context_packet(const struct vita49_2_context_packet *pkt, uint32_t *buf, size_t max_words);

/**
 * @brief Parses a buffer of 32-bit words into a vita49_2_context_packet structure. 
 * Returns 0 on success, or a negative error code (e.g. -EINVAL) on failure.
 * 
 * @param buf 
 * @param words 
 * @param pkt 
 * @return int 
 */
__vrt_api int vita49_2_parse_context_packet(const uint32_t *buf, size_t words, struct vita49_2_context_packet *pkt);

/**
 * @brief Populates a 32-bit word buffer with data for a Control Packet. 
 * The buffer MUST be large enough to hold the generated packet.
 * Returns the number of words written, or a negative error code on failure.
 * 
 * @param pkt 
 * @param buf 
 * @param max_words 
 * @return ssize_t 
 */
__vrt_api ssize_t vita49_2_generate_control_packet(const struct vita49_2_control_packet *pkt, uint32_t *buf, size_t max_words);


/**
 * @brief Parses a buffer of 32-bit words into a vita49_2_control_packet structure. 
 * Returns 0 on success, or a negative error code (e.g. -EINVAL) on failure.
 * 
 * @param buf 
 * @param words 
 * @param pkt 
 * @return int 
 */
__vrt_api int vita49_2_parse_control_packet(const uint32_t *buf, size_t words, struct vita49_2_control_packet *pkt);

#endif /* __VITA49_2_PACKET_TYPES_H__ */
