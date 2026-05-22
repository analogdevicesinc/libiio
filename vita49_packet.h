/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Travis Collins <travis.collins@analog.com>
 */

#ifndef __VITA49_PACKET_H__
#define __VITA49_PACKET_H__

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

/**
 * @struct vrt_cif_fields
 * @brief Parsed representation of Context Indicator Field 0 (CIF0) payload.
 *
 * This structure holds the decoded information from an IF Context packet's
 * payload, representing varying system and signal state attributes.
 */
struct vrt_cif_fields {
	uint32_t cif0; /**< The raw Context Indicator Field 0 word */

	bool  context_field_change; /**< True if any context field has changed (Bit 31) */
	
	bool  has_reference_point_id; /**< True if Reference Point Identifier is present (Bit 30) */
	uint32_t reference_point_id;  /**< Reference Point Identifier */
	
	bool  has_bandwidth;          /**< True if Bandwidth is present (Bit 29) */
	double bandwidth;             /**< Bandwidth in Hz */
	
	bool  has_if_reference_frequency; /**< True if IF Reference Frequency is present (Bit 28) */
	double if_reference_frequency;    /**< IF Reference Frequency in Hz */
	
	bool  has_rf_reference_frequency; /**< True if RF Reference Frequency is present (Bit 27) */
	double rf_reference_frequency;    /**< RF Reference Frequency in Hz */
	
	bool  has_rf_reference_frequency_offset; /**< True if RF Reference Frequency Offset is present (Bit 26) */
	double rf_reference_frequency_offset;    /**< RF Reference Frequency Offset in Hz */
	
	bool  has_if_band_offset;     /**< True if IF Band Offset is present (Bit 25) */
	double if_band_offset;        /**< IF Band Offset in Hz */
	
	bool  has_reference_level;    /**< True if Reference Level is present (Bit 24) */
	float reference_level;        /**< Reference Level in dBm */
	
	bool  has_gain;               /**< True if Gain is present (Bit 23) */
	float gain_stage_1;           /**< Gain Stage 1 in dB */
	float gain_stage_2;           /**< Gain Stage 2 in dB */
	
	bool  has_over_range_count;   /**< True if Over-Range Count is present (Bit 22) */
	uint32_t over_range_count;    /**< Over-Range Count */
	
	bool  has_sample_rate;        /**< True if Sample Rate is present (Bit 21) */
	double sample_rate;           /**< Sample Rate in Hz */
	
	bool  has_timestamp_adjustment; /**< True if Timestamp Adjustment is present (Bit 20) */
	uint64_t timestamp_adjustment;  /**< Timestamp Adjustment in picoseconds */
	
	bool  has_timestamp_calibration_time;     /**< True if Timestamp Calibration Time is present (Bit 19) */
	uint32_t timestamp_calibration_time_int;  /**< Integer part of Calibration Time */
	uint64_t timestamp_calibration_time_frac; /**< Fractional part of Calibration Time */
	
	bool  has_temperature;        /**< True if Temperature is present (Bit 18) */
	float temperature;            /**< Temperature in degrees Celsius */
	
	bool  has_device_identifier;     /**< True if Device Identifier is present (Bit 17) */
	uint32_t device_identifier_oui;  /**< Device Identifier OUI (24-bit) */
	uint16_t device_identifier_code; /**< Device Identifier Code (16-bit) */
	
	bool  has_state_and_event_indicators; /**< True if State/Event Indicators are present (Bit 16) */
	uint32_t state_and_event_indicators;  /**< State and Event indicators bitmap */
	
	bool  has_data_packet_payload_format; /**< True if Data Packet Payload Format is present (Bit 15) */
	uint64_t data_packet_payload_format;  /**< Payload Format specific bits */
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
