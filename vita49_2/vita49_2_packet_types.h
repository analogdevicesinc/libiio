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

// To avoid conflicting Stream IDs (32 bit values) for Signal Data Packets based on data direction (sent from host/sent from device), 
// I’ve allocated non-overlapping intervals (each of size 2^32 / 2 numbers) for the host and module.
// Furthermore, I’ve halved the first region to split it between time and spectral data. 
// Time packets gets the first half of the available Stream IDs while spectral packets get the second half.

// Acceptable range of values for Stream ID for Signal Time Data Packets.
#define VITA49_2_PKT_TIME_DATA_DEVICE_START 0
#define VITA49_2_PKT_TIME_DATA_DEVICE_END 1073741823

// Acceptable range of values for Stream ID for Signal Spectral Data Packets.
#define VITA49_2_PKT_SPECTRAL_DATA_DEVICE_START 1073741824
#define VITA49_2_PKT_SPECTRAL_DATA_DEVICE_END 2147483647

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
	struct vita49_2_cif1_fields* cif1;	/* CIF1 Word and Fields */
	struct vita49_2_cif2_fields* cif2;	/* CIF2 Word and Fields */
	struct vita49_2_cif3_fields* cif3;	/* CIF3 Word and Fields */
	struct vita49_2_cif7_fields* cif7;	/* CIF7 Word and Fields */

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
 * @brief Represents a parsed VITA 49.2 AckX Packet.
 * 
 * This structure holds all decompressed fields and metadata of a 
 * VITA 49.2 AckX Packet, providing easy access to components.
 */
struct vita49_2_ackX_packet {
	
	struct vita49_2_command_prologue command_prologue;	/* Common fields present in every VITA 49.2 Command Packet */
	

	/* WARNINGS */
	struct cif0_word cif0_warnings;	/* Indicates which of the CIF0 attributes had warnings */

	// Not currently in use, but ADI retains the right to implement these fields in the future.
	// Using pointers since embedding each of these structs would result in a lot of bloat if they're unused.
	struct cif1_word* cif1_warnings;	
	struct cif2_word* cif2_warnings;
	struct cif3_word* cif3_warnings;
	struct cif7_word* cif7_warnings;

	// NOTE: The enable bits for CIF1-7 are contained within the cif0_word parameter in the cif0_warnings struct.
	// DO NOT duplicate those enable bits here so we can avoid mismatching states. 
	// Always access those enables through the cif0_warnings struct.


	/* ERRORS */
	struct cif0_word cif0_errors;	/* Indicates which of the CIF0 had errors */
	struct cif1_word* cif1_errors;	
	struct cif2_word* cif2_errors;
	struct cif3_word* cif3_errors;
	struct cif7_word* cif7_errors;


	// Each CIF field that produced a warning gets a 32-bit warning indicator word in the payload
	// to indicate what kind of warning occured (see Table 8.4.1.2.1-1 in the VITA 49.2 full spec document
	// for the list of predefined warnings/errors)
	struct vita49_2_warning_error_indicators* warnings_payload; 	/* Pointer to the start of the payload words */
	uint16_t warnings_payload_num_words;     					/* Number of 32-bit words in the payload */

	// Same as the warning fields, but for errors
	struct vita49_2_warning_error_indicators* errors_payload;  	/* Pointer to the start of the payload words */
	uint16_t errors_payload_num_words;     						/* Number of 32-bit words in the payload */

};

/**
 * @struct vita49_2_ackV_packet
 * @brief Represents a parsed VITA 49.2 AckV Packet.
 * 
 * This structure holds all decompressed fields and metadata of a 
 * VITA 49.2 AckV Packet, providing easy access to components.
 */
struct vita49_2_ackV_packet {
	
	struct vita49_2_command_prologue command_prologue;	/* Common fields present in every VITA 49.2 Command Packet */
	
	/* WARNINGS */
	struct cif0_word cif0_warnings;	/* Indicates which of the CIF0 attributes had warnings */

	// Not currently in use, but ADI retains the right to implement these fields in the future.
	// Using pointers since embedding each of these structs would result in a lot of bloat if they're unused.
	struct cif1_word* cif1_warnings;	
	struct cif2_word* cif2_warnings;
	struct cif3_word* cif3_warnings;
	struct cif7_word* cif7_warnings;

	// NOTE: The enable bits for CIF1-7 are contained within the cif0_word parameter in the cif0_warnings struct.
	// DO NOT duplicate those enable bits here so we can avoid mismatching states. 
	// Always access those enables through the cif0_warnings struct.

	// AckV messages aren't allowed to have error fields unlike AckX

	// Each CIF field that produced a warning gets a 32-bit warning indicator word in the payload
	// to indicate what kind of warning occured (see Table 8.4.1.2.1-1 in the VITA 49.2 full spec document
	// for the list of predefined warnings/errors)
	struct vita49_2_warning_error_indicators* warnings_payload; 	/* Pointer to the start of the payload words */
	uint16_t warnings_payload_num_words;     					/* Number of 32-bit words in the payload */
};

/**
 * @struct vita49_2_ackS_packet
 * @brief Represents a parsed VITA 49.2 AckS Packet.
 * 
 * This structure holds all decompressed fields and metadata of a 
 * VITA 49.2 AckS Packet, providing easy access to components.
 */
struct vita49_2_ackS_packet {
	
	struct vita49_2_command_prologue command_prologue;	/* Common fields present in every VITA 49.2 Command Packet */
	
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

/**
 * @brief Populates a 32-bit word buffer with data for an AckX Packet. 
 * The buffer MUST be large enough to hold the generated packet.
 * Returns the number of words written, or a negative error code on failure.
 * 
 * @param pkt 
 * @param buf 
 * @param max_words 
 * @return ssize_t 
 */
__vrt_api ssize_t vita49_2_generate_ackx_packet(const struct vita49_2_ackX_packet *pkt, uint32_t *buf, size_t max_words);

/**
 * @brief Parses a buffer of 32-bit words into a vita49_2_ackv_ackx_packet structure. 
 * Returns 0 on success, or a negative error code (e.g. -EINVAL) on failure.
 * 
 * @param buf 
 * @param words 
 * @param pkt 
 * @return int 
 */
__vrt_api int vita49_2_parse_ackx_packet(const uint32_t *buf, size_t words, struct vita49_2_ackX_packet *pkt);

/**
 * @brief Populates a 32-bit word buffer with data for a AckS Packet. 
 * The buffer MUST be large enough to hold the generated packet.
 * Returns the number of words written, or a negative error code on failure.
 * 
 * @param pkt 
 * @param buf 
 * @param max_words 
 * @return ssize_t 
 */
__vrt_api ssize_t vita49_2_generate_acks_packet(const struct vita49_2_ackS_packet *pkt, uint32_t *buf, size_t max_words);

/**
 * @brief Parses a buffer of 32-bit words into a vita49_2_acks_packet structure. 
 * Returns 0 on success, or a negative error code (e.g. -EINVAL) on failure.
 * 
 * @param buf 
 * @param words 
 * @param pkt 
 * @return int 
 */
__vrt_api int vita49_2_parse_acks_packet(const uint32_t *buf, size_t words, struct vita49_2_ackS_packet *pkt);

#endif /* __VITA49_2_PACKET_TYPES_H__ */
