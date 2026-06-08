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

#ifndef __VITA49_PACKET_TYPES_H__
#define __VITA49_PACKET_TYPES_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include "include/iio/iio.h"
#include "vita49.h"

/**
 * @struct vrt_packet
 * @brief Represents a parsed VITA 49.2 packet.
 *
 * This structure holds all decompressed fields and metadata of a 
 * VITA 49.2 VRT packet, providing easy access to components.
 */
struct vrt_packet {
	struct vrt_header header; /**< Standard VITA 49.2 32-bit header */
	uint32_t stream_id;       /**< Optional 32-bit Stream Identifier */
	uint64_t class_id;        /**< Optional 64-bit Class Identifier (OUI 24-bit + Info Class 16-bit + Packet Class 16-bit) */
	uint32_t timestamp_int;   /**< Optional Integer Timestamp */
	uint64_t timestamp_frac;  /**< Optional Fractional Timestamp */
	
	const uint32_t *payload;  /**< Pointer to the start of the payload words */
	size_t payload_words;     /**< Number of 32-bit words in the payload */
	
	struct vrt_trailer trailer; /**< Optional 32-bit trailer */

	bool has_stream_id;       /**< True if stream_id is populated */
	bool has_class_id;        /**< True if class_id is populated */
	bool has_timestamp_int;   /**< True if timestamp_int is populated */
	bool has_timestamp_frac;  /**< True if timestamp_frac is populated */
	bool has_trailer;         /**< True if trailer is populated */
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

/* Parses a buffer of 32-bit words into a vrt_packet structure. 
 * Returns 0 on success, or a negative error code (e.g. -EINVAL) on failure.
 */
__vrt_api int vrt_parse_packet(const uint32_t *buf, size_t words, struct vrt_packet *pkt);

/* Generates a buffer of 32-bit words from a vrt_packet structure.
 * The buffer must be large enough to hold the generated packet.
 * Returns the number of words written, or a negative error code on failure.
 */
__vrt_api ssize_t vrt_generate_packet(const struct vrt_packet *pkt, uint32_t *buf, size_t max_words);

/* Extracts a 32-bit word from the packet payload, handling network byte-order translation. */
__vrt_api uint32_t vrt_get_payload_word(const struct vrt_packet *pkt, size_t offset);

/* Inserts a 32-bit word into a raw payload buffer in network byte-order. */
__vrt_api void vrt_set_payload_word(uint32_t *payload, size_t max_words, size_t offset, uint32_t val);

/* Extracts an IEEE 754 64-bit float from the packet payload, handling network byte-order translation. */
__vrt_api double vrt_get_payload_double(const struct vrt_packet *pkt, size_t offset);

/* Inserts an IEEE 754 64-bit float into a raw payload buffer in network byte-order. */
__vrt_api void vrt_set_payload_double(uint32_t *payload, size_t max_words, size_t offset, double val);

/* Parses the CIF0 payload section if the packet is of type IF_CONTEXT.
 * Evaluates the flags present in CIF0 to sequentially decode the context payload.
 */
__vrt_api int vrt_parse_cif_payload(const struct vrt_packet *pkt, struct vrt_cif_fields *cif);

#endif /* __VITA49_PACKET_H__ */
