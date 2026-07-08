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
#define VITA49_2_PKT_TIME_DATA_DEVICE_END 2147483647

/**
 * @struct vita49_2_data_packet
 * @brief Represents a parsed VITA 49.2 Signal Data Packet.
 *
 * This structure holds all decompressed fields and metadata of a 
 * VITA 49.2 Signal Data Packet, providing easy access to components.
 */
struct vita49_2_data_packet {
	
	struct vita49_2_prologue prologue;	/* Common fields present in every VITA 49.2 packet */
	
	union vita49_2_iq_item *payload;  	/* Pointer to the start of the payload words */
	uint16_t payload_num_words;     	/* Number of 32-bit words in the payload */
	
	union vita49_2_trailer trailer; 	/* Optional 32-bit trailer */
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
	struct vita49_2_cif1_fields* cif1;	/* CIF1 Word and Fields */
	struct vita49_2_cif2_fields* cif2;	/* CIF2 Word and Fields */
	struct vita49_2_cif3_fields* cif3;	/* CIF3 Word and Fields */
	struct vita49_2_cif7_fields* cif7;	/* CIF7 Word and Fields */

	// NOTE: The enable bits for CIF1-7 are contained within the cif0_word parameter in the cif0 struct.
	// DO NOT duplicate those enable bits here so we can avoid mismatching states. 
	// Always access those enables through the cif0 struct.

};

/**
 * @struct vita49_2_ackX_packet
 * @brief Represents a parsed VITA 49.2 AckX Packet.
 * 
 * This structure holds all decompressed fields and metadata of a 
 * VITA 49.2 AckX Packet, providing easy access to components.
 */
struct vita49_2_ackX_packet {
	
	struct vita49_2_command_prologue command_prologue;	/* Common fields present in every VITA 49.2 Command Packet */
	
	// NOTE: The enable bits for CIF1-7 are contained within the cif0_word parameter in the cif0_warnings struct.
	// DO NOT duplicate those enable bits here so we can avoid mismatching states. 
	// Always access those enables through the cif0_warnings struct.

	struct vita49_2_warnings warnings;
	struct vita49_2_errors errors;

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
	struct vita49_2_warnings warnings;

	// NOTE: The enable bits for CIF1-7 are contained within the cif0_word parameter in the cif0_warnings struct.
	// DO NOT duplicate those enable bits here so we can avoid mismatching states. 
	// Always access those enables through the cif0_warnings struct.

	// AckV messages aren't allowed to have error fields unlike AckX

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
	struct vita49_2_cif1_fields* cif1;	/* CIF1 Word and Fields */
	struct vita49_2_cif2_fields* cif2;	/* CIF2 Word and Fields */
	struct vita49_2_cif3_fields* cif3;	/* CIF3 Word and Fields */
	struct vita49_2_cif7_fields* cif7;	/* CIF7 Word and Fields */

};

/**
 * @struct vita49_2_control_extension_packet
 * @brief Represents a parsed VITA 49.2 Command Extension Packet.
 * 
 * This structure holds all decompressed fields and metadata of a 
 * VITA 49.2 Command Extension Packet, providing easy access to components.
 * 
 * The purpose of this Command Extension Packet is to issue commands/controls on ADI devices that can't
 * be well translated to CIF0-7 fields.
 */
struct vita49_2_control_extension_packet {
	
	struct vita49_2_command_prologue command_prologue;	/* Common fields present in every VITA 49.2 Command Packet */
	
	// ADI SDRs differ in what kind of IIO attributes they have, especially if personality cards are used.
	// Therefore we can't rely on a fixed size array of extension word unions, rather we'll need a linked list.
	struct vita49_2_control_extension_word_node *payload;  	/* Head node */

};

/**
 * @struct vita49_2_ackV_extension_packet
 * @brief Represents a parsed VITA 49.2 AckV Extension Packet.
 * 
 * This structure holds all decompressed fields and metadata of a 
 * VITA 49.2 AckV Extension Packet, providing easy access to components.
 */
struct vita49_2_ackV_extension_packet {
	
	struct vita49_2_command_prologue command_prologue;	/* Common fields present in every VITA 49.2 Command Packet */
	
	// ADI SDRs differ in what kind of IIO attributes they have, especially if personality cards are used.
	// Therefore we can't rely on a fixed size array of extension word unions, rather we'll need a linked list.
	struct vita49_2_ackV_extension_word_node *payload;  	/* Head node */

};

/**
 * @struct vita49_2_ackX_extension_packet
 * @brief Represents a parsed VITA 49.2 AckX Extension Packet.
 * 
 * This structure holds all decompressed fields and metadata of a 
 * VITA 49.2 AckX Extension Packet, providing easy access to components.
 */
struct vita49_2_ackX_extension_packet {
	
	struct vita49_2_command_prologue command_prologue;	/* Common fields present in every VITA 49.2 Command Packet */
	
	// ADI SDRs differ in what kind of IIO attributes they have, especially if personality cards are used.
	// Therefore we can't rely on a fixed size array of extension word unions, rather we'll need a linked list.
	struct vita49_2_ackX_extension_word_node *payload;  	/* Head node */
	
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
 * @param pkt Pointer to the data packet.
 * @param buf Pointer to the destination buffer.
 * @param max_words Max size of the destination buffer that the packet gets serialized to.
 * @return ssize_t 
 */
__vrt_api ssize_t vita49_2_serialize_data_packet(struct vita49_2_data_packet* const pkt, uint32_t* const buf, size_t max_words);

/**
 * @brief Parses a buffer of 32-bit words into a vita49_2_data_packet structure. 
 * Returns 0 on success, or a negative error code (e.g. -EINVAL) on failure.
 * 
 * @param buf Pointer to the source buffer.
 * @param buf_words Size of the buffer in 32-bit words.
 * @param pkt Pointer to a data packet that is to be populated.
 * @param with_stream_id True if this is a Data Packet that includes Stream ID (Packet Type = 1)
 * @return int 
 */
__vrt_api int vita49_2_parse_data_packet(const uint32_t* const buf, size_t buf_words, struct vita49_2_data_packet* const pkt, bool with_stream_id);

/**
 * @brief Populates a 32-bit word buffer with data for a Context Packet. 
 * The buffer MUST be large enough to hold the generated packet.
 * Returns the number of words written, or a negative error code on failure.
 * 
 * @param pkt Pointer to the context packet.
 * @param buf Pointer to the destination buffer.
 * @param max_words Max size of the destination buffer that the packet gets serialized to. 
 * @return ssize_t 
 */
__vrt_api ssize_t vita49_2_serialize_context_packet(struct vita49_2_context_packet* const pkt, uint32_t* const buf, size_t max_words);

/**
 * @brief Parses a buffer of 32-bit words into a vita49_2_context_packet structure. 
 * Returns 0 on success, or a negative error code (e.g. -EINVAL) on failure.
 * 
 * @param buf Pointer to the source buffer.
 * @param buf_words Size of the buffer in 32-bit words.
 * @param pkt Pointer to a context packet that is to be populated.
 * @return int 
 */
__vrt_api int vita49_2_parse_context_packet(const uint32_t* const buf, size_t buf_words, struct vita49_2_context_packet* const pkt);

/**
 * @brief Populates a 32-bit word buffer with data for a Control Packet. 
 * The buffer MUST be large enough to hold the generated packet.
 * Returns the number of words written, or a negative error code on failure.
 * 
 * @param pkt Pointer to the control packet.
 * @param buf Pointer to the destination buffer.
 * @param max_words Max size of the destination buffer that the packet gets serialized to. 
 * @return ssize_t 
 */
__vrt_api ssize_t vita49_2_serialize_control_packet(struct vita49_2_control_packet* const pkt, uint32_t* const buf, size_t max_words);

/**
 * @brief Parses a buffer of 32-bit words into a vita49_2_control_packet structure. 
 * Returns 0 on success, or a negative error code (e.g. -EINVAL) on failure.
 * 
 * @param buf Pointer to the source buffer.
 * @param buf_words Size of the buffer in 32-bit words.
 * @param pkt Pointer to a control packet that is to be populated.
 * @return int 
 */
__vrt_api int vita49_2_parse_control_packet(const uint32_t* const buf, size_t buf_words, struct vita49_2_control_packet* const pkt);

/**
 * @brief Populates a 32-bit word buffer with data for an AckX Packet. 
 * The buffer MUST be large enough to hold the generated packet.
 * Returns the number of words written, or a negative error code on failure.
 * 
 * @param pkt Pointer to the ackX packet.
 * @param buf Pointer to the destination buffer.
 * @param max_words Max size of the destination buffer that the packet gets serialized to. 
 * @return ssize_t 
 */
__vrt_api ssize_t vita49_2_serialize_ackX_packet(struct vita49_2_ackX_packet* const pkt, uint32_t* const buf, size_t max_words);

/**
 * @brief Parses a buffer of 32-bit words into a vita49_2_ackx_packet structure. 
 * Returns 0 on success, or a negative error code (e.g. -EINVAL) on failure.
 * 
 * @param buf Pointer to the source buffer.
 * @param buf_words Size of the buffer in 32-bit words.
 * @param pkt Pointer to a ackX packet that is to be populated.
 * @return int 
 */
__vrt_api int vita49_2_parse_ackX_packet(const uint32_t* const buf, size_t buf_words, struct vita49_2_ackX_packet* const pkt);

/**
 * @brief Populates a 32-bit word buffer with data for an AckV Packet. 
 * The buffer MUST be large enough to hold the generated packet.
 * Returns the number of words written, or a negative error code on failure.
 * 
 * @param pkt Pointer to the ackV packet.
 * @param buf Pointer to the destination buffer.
 * @param max_words Max size of the destination buffer that the packet gets serialized to.  
 * @return ssize_t 
 */
__vrt_api ssize_t vita49_2_serialize_ackV_packet(struct vita49_2_ackV_packet* const pkt, uint32_t* const buf, size_t max_words);

/**
 * @brief Parses a buffer of 32-bit words into a vita49_2_ackv_packet structure. 
 * Returns 0 on success, or a negative error code (e.g. -EINVAL) on failure.
 * 
 * @param buf Pointer to the source buffer.
 * @param buf_words Size of the buffer in 32-bit words.
 * @param pkt Pointer to a ackV packet that is to be populated.
 * @return int 
 */
__vrt_api int vita49_2_parse_ackV_packet(const uint32_t* const buf, size_t buf_words, struct vita49_2_ackV_packet* const pkt);

/**
 * @brief Populates a 32-bit word buffer with data for a AckS Packet. 
 * The buffer MUST be large enough to hold the generated packet.
 * Returns the number of words written, or a negative error code on failure.
 * 
 * @param pkt Pointer to the ackS packet.
 * @param buf Pointer to the destination buffer.
 * @param max_words Max size of the destination buffer that the packet gets serialized to. 
 * @return ssize_t 
 */
__vrt_api ssize_t vita49_2_serialize_ackS_packet(struct vita49_2_ackS_packet* const pkt, uint32_t* const buf, size_t max_words);

/**
 * @brief Parses a buffer of 32-bit words into a vita49_2_acks_packet structure. 
 * Returns 0 on success, or a negative error code (e.g. -EINVAL) on failure.
 * 
 * @param buf Pointer to the source buffer.
 * @param buf_words Size of the buffer in 32-bit words.
 * @param pkt Pointer to a ackS packet that is to be populated.
 * @return int 
 */
__vrt_api int vita49_2_parse_ackS_packet(const uint32_t* const buf, size_t buf_words, struct vita49_2_ackS_packet* const pkt);

/**
 * @brief Populates a 32-bit word buffer with data for a Control Extension Packet. 
 * The buffer MUST be large enough to hold the generated packet.
 * Returns the number of words written, or a negative error code on failure.
 * 
 * @param pkt Pointer to the control extension packet.
 * @param buf Pointer to the destination buffer.
 * @param max_words Max size of the destination buffer that the packet gets serialized to. 
 * @return ssize_t 
 */
__vrt_api ssize_t vita49_2_serialize_control_extension_packet(struct vita49_2_control_extension_packet* const pkt, uint32_t* const buf, size_t max_words);

/**
 * @brief Parses a buffer of 32-bit words into a vita49_2_control_extension_packet structure. 
 * Returns 0 on success, or a negative error code (e.g. -EINVAL) on failure.
 * 
 * @param buf Pointer to the source buffer.
 * @param buf_words Size of the buffer in 32-bit words.
 * @param pkt Pointer to a control extension packet that is to be populated.
 * @return int 
 */
__vrt_api int vita49_2_parse_control_extension_packet(const uint32_t* const buf, size_t buf_words, struct vita49_2_control_extension_packet* const pkt);


#endif /* __VITA49_2_PACKET_TYPES_H__ */
