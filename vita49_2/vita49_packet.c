/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Travis Collins <travis.collins@analog.com>
 */

#include "vita49_packet.h"
#include <errno.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

int vrt_parse_packet(const uint32_t *buf, size_t words, struct vrt_packet *pkt)
{
	if (!buf || !pkt || words == 0)
		return -EINVAL;

	memset(pkt, 0, sizeof(*pkt));

	uint32_t header_word = ntohl(buf[0]);
	memcpy(&pkt->header, &header_word, sizeof(uint32_t));

	if (pkt->header.packet_size_words > words)
		return -EINVAL; /* Buffer too small for packet size */

	size_t idx = 1;

	/* Determine if Stream ID is present */
	bool has_sid = (pkt->header.packet_type == VRT_PKT_TYPE_IF_DATA_WITH_SID ||
			pkt->header.packet_type == VRT_PKT_TYPE_EXT_DATA_WITH_SID ||
			pkt->header.packet_type == VRT_PKT_TYPE_IF_CONTEXT ||
			pkt->header.packet_type == VRT_PKT_TYPE_EXT_CONTEXT ||
			pkt->header.packet_type == VRT_PKT_TYPE_COMMAND ||
			pkt->header.packet_type == VRT_PKT_TYPE_EXT_COMMAND);

	if (has_sid) {
		if (idx >= pkt->header.packet_size_words) return -EINVAL;
		pkt->stream_id = ntohl(buf[idx++]);
		pkt->has_stream_id = true;
	}

	/* Class ID */
	if (pkt->header.has_class_id) {
		if (idx + 1 >= pkt->header.packet_size_words) return -EINVAL;
		uint32_t w1 = ntohl(buf[idx++]);
		uint32_t w2 = ntohl(buf[idx++]);
		pkt->class_id = ((uint64_t)w1 << 32) | w2;
		pkt->has_class_id = true;
	}

	/* Timestamp Integer */
	if (pkt->header.ts_integer_format != VRT_TSI_NONE) {
		if (idx >= pkt->header.packet_size_words) return -EINVAL;
		pkt->timestamp_int = ntohl(buf[idx++]);
		pkt->has_timestamp_int = true;
	}

	/* Timestamp Fractional */
	if (pkt->header.ts_fractional_format != VRT_TSF_NONE) {
		if (idx + 1 >= pkt->header.packet_size_words) return -EINVAL;
		uint32_t w1 = ntohl(buf[idx++]);
		uint32_t w2 = ntohl(buf[idx++]);
		pkt->timestamp_frac = ((uint64_t)w1 << 32) | w2;
		pkt->has_timestamp_frac = true;
	}

	/* Trailer */
	if (pkt->header.has_trailer) {
		if (pkt->header.packet_size_words < idx + 1) return -EINVAL;
		uint32_t trailer_word = ntohl(buf[pkt->header.packet_size_words - 1]);
		memcpy(&pkt->trailer, &trailer_word, sizeof(uint32_t));
		pkt->has_trailer = true;
		
		pkt->payload = &buf[idx];
		pkt->payload_words = pkt->header.packet_size_words - idx - 1;
	} else {
		pkt->payload = &buf[idx];
		pkt->payload_words = pkt->header.packet_size_words - idx;
	}

	return 0;
}

ssize_t vrt_generate_packet(const struct vrt_packet *pkt, uint32_t *buf, size_t max_words)
{
	if (!pkt || !buf)
		return -EINVAL;

	size_t idx = 1;
	uint32_t header_word;
	memcpy(&header_word, &pkt->header, sizeof(uint32_t));

	/* Stream ID */
	bool has_sid = (pkt->header.packet_type == VRT_PKT_TYPE_IF_DATA_WITH_SID ||
			pkt->header.packet_type == VRT_PKT_TYPE_EXT_DATA_WITH_SID ||
			pkt->header.packet_type == VRT_PKT_TYPE_IF_CONTEXT ||
			pkt->header.packet_type == VRT_PKT_TYPE_EXT_CONTEXT ||
			pkt->header.packet_type == VRT_PKT_TYPE_COMMAND ||
			pkt->header.packet_type == VRT_PKT_TYPE_EXT_COMMAND);

	if (has_sid) {
		if (idx >= max_words) return -ENOBUFS;
		buf[idx++] = htonl(pkt->stream_id);
	}

	/* Class ID */
	if (pkt->header.has_class_id) {
		if (idx + 1 >= max_words) return -ENOBUFS;
		buf[idx++] = htonl((uint32_t)(pkt->class_id >> 32));
		buf[idx++] = htonl((uint32_t)(pkt->class_id & 0xFFFFFFFF));
	}

	/* Timestamp Int */
	if (pkt->header.ts_integer_format != VRT_TSI_NONE) {
		if (idx >= max_words) return -ENOBUFS;
		buf[idx++] = htonl(pkt->timestamp_int);
	}

	/* Timestamp Frac */
	if (pkt->header.ts_fractional_format != VRT_TSF_NONE) {
		if (idx + 1 >= max_words) return -ENOBUFS;
		buf[idx++] = htonl((uint32_t)(pkt->timestamp_frac >> 32));
		buf[idx++] = htonl((uint32_t)(pkt->timestamp_frac & 0xFFFFFFFF));
	}

	/* Payload */
	if (pkt->payload && pkt->payload_words > 0) {
		if (idx + pkt->payload_words > (max_words - (pkt->header.has_trailer ? 1 : 0))) return -ENOBUFS;
		/* Assume big-endian payload words */
		memcpy(&buf[idx], pkt->payload, pkt->payload_words * sizeof(uint32_t));
		idx += pkt->payload_words;
	}

	/* Trailer */
	if (pkt->header.has_trailer) {
		if (idx >= max_words) return -ENOBUFS;
		uint32_t trailer_word;
		memcpy(&trailer_word, &pkt->trailer, sizeof(uint32_t));
		buf[idx++] = htonl(trailer_word);
	}

	/* Update size in header */
	struct vrt_header final_hdr = pkt->header;
	final_hdr.packet_size_words = idx;
	memcpy(&header_word, &final_hdr, sizeof(uint32_t));
	buf[0] = htonl(header_word);

	return idx;
}

uint32_t vrt_get_payload_word(const struct vrt_packet *pkt, size_t offset)
{
	if (!pkt || !pkt->payload || offset >= pkt->payload_words)
		return 0;
	return ntohl(pkt->payload[offset]);
}

void vrt_set_payload_word(uint32_t *payload, size_t max_words, size_t offset, uint32_t val)
{
	if (!payload || offset >= max_words)
		return;
	payload[offset] = htonl(val);
}

double vrt_get_payload_double(const struct vrt_packet *pkt, size_t offset)
{
	if (!pkt || !pkt->payload || offset + 1 >= pkt->payload_words)
		return 0.0;
	uint32_t w1 = ntohl(pkt->payload[offset]);
	uint32_t w2 = ntohl(pkt->payload[offset + 1]);
	uint64_t v_int = ((uint64_t)w1 << 32) | w2;
	double val;
	memcpy(&val, &v_int, sizeof(double));
	return val;
}

void vrt_set_payload_double(uint32_t *payload, size_t max_words, size_t offset, double val)
{
	if (!payload || offset + 1 >= max_words)
		return;
	uint64_t v_int;
	memcpy(&v_int, &val, sizeof(double));
	payload[offset] = htonl((uint32_t)(v_int >> 32));
	payload[offset + 1] = htonl((uint32_t)(v_int & 0xFFFFFFFF));
}

int vrt_parse_cif_payload(const struct vrt_packet *pkt, struct vrt_cif_fields *cif)
{
	size_t offset = 1;

	if (!pkt || !cif)
		return -1;

	memset(cif, 0, sizeof(*cif));

	if (pkt->header.packet_type != VRT_PKT_TYPE_IF_CONTEXT &&
	    pkt->header.packet_type != VRT_PKT_TYPE_EXT_CONTEXT)
		return -1;

	if (pkt->payload_words < 1)
		return -1;

	cif->cif0 = vrt_get_payload_word(pkt, 0);

	if (cif->cif0 & (1 << 31)) {
		cif->context_field_change = true;
	}
	if (cif->cif0 & (1 << 30)) {
		cif->has_reference_point_id = true;
		cif->reference_point_id = vrt_get_payload_word(pkt, offset);
		offset += 1;
	}
	if (cif->cif0 & (1 << 29)) {
		cif->has_bandwidth = true;
		cif->bandwidth = vrt_get_payload_double(pkt, offset);
		offset += 2;
	}
	if (cif->cif0 & (1 << 28)) {
		cif->has_if_reference_frequency = true;
		cif->if_reference_frequency = vrt_get_payload_double(pkt, offset);
		offset += 2;
	}
	if (cif->cif0 & (1 << 27)) {
		cif->has_rf_reference_frequency = true;
		cif->rf_reference_frequency = vrt_get_payload_double(pkt, offset);
		offset += 2;
	}
	if (cif->cif0 & (1 << 26)) {
		cif->has_rf_reference_frequency_offset = true;
		cif->rf_reference_frequency_offset = vrt_get_payload_double(pkt, offset);
		offset += 2;
	}
	if (cif->cif0 & (1 << 25)) {
		cif->has_if_band_offset = true;
		cif->if_band_offset = vrt_get_payload_double(pkt, offset);
		offset += 2;
	}
	if (cif->cif0 & (1 << 24)) {
		cif->has_reference_level = true;
		uint32_t val = vrt_get_payload_word(pkt, offset);
		float fval;
		memcpy(&fval, &val, sizeof(float));
		cif->reference_level = fval;
		offset += 1;
	}
	if (cif->cif0 & (1 << 23)) {
		cif->has_gain = true;
		uint32_t val = vrt_get_payload_word(pkt, offset);
		int16_t stage1 = val >> 16;
		int16_t stage2 = val & 0xFFFF;
		cif->gain_stage_1 = (float)stage1;
		cif->gain_stage_2 = (float)stage2;
		offset += 1;
	}
	if (cif->cif0 & (1 << 22)) {
		cif->has_over_range_count = true;
		cif->over_range_count = vrt_get_payload_word(pkt, offset);
		offset += 1;
	}
	if (cif->cif0 & (1 << 21)) {
		cif->has_sample_rate = true;
		cif->sample_rate = vrt_get_payload_double(pkt, offset);
		offset += 2;
	}
	if (cif->cif0 & (1 << 20)) {
		cif->has_timestamp_adjustment = true;
		uint32_t w1 = vrt_get_payload_word(pkt, offset);
		uint32_t w2 = vrt_get_payload_word(pkt, offset + 1);
		cif->timestamp_adjustment = ((uint64_t)w1 << 32) | w2;
		offset += 2;
	}
	if (cif->cif0 & (1 << 19)) {
		cif->has_timestamp_calibration_time = true;
		cif->timestamp_calibration_time_int = vrt_get_payload_word(pkt, offset);
		uint32_t f1 = vrt_get_payload_word(pkt, offset + 1);
		uint32_t f2 = vrt_get_payload_word(pkt, offset + 2);
		cif->timestamp_calibration_time_frac = ((uint64_t)f1 << 32) | f2;
		offset += 3;
	}
	if (cif->cif0 & (1 << 18)) {
		cif->has_temperature = true;
		uint32_t val = vrt_get_payload_word(pkt, offset);
		int16_t integer = val >> 16;
		uint16_t frac = val & 0xFFFF;
		cif->temperature = (float)integer + ((float)frac / 65536.0f);
		offset += 1;
	}
	if (cif->cif0 & (1 << 17)) {
		cif->has_device_identifier = true;
		uint32_t oui = vrt_get_payload_word(pkt, offset);
		uint16_t code = vrt_get_payload_word(pkt, offset + 1) >> 16;
		cif->device_identifier_oui = oui & 0xFFFFFF;
		cif->device_identifier_code = code;
		offset += 2;
	}
	if (cif->cif0 & (1 << 16)) {
		cif->has_state_and_event_indicators = true;
		cif->state_and_event_indicators = vrt_get_payload_word(pkt, offset);
		offset += 1;
	}
	if (cif->cif0 & (1 << 15)) {
		cif->has_data_packet_payload_format = true;
		uint32_t w1 = vrt_get_payload_word(pkt, offset);
		uint32_t w2 = vrt_get_payload_word(pkt, offset + 1);
		cif->data_packet_payload_format = ((uint64_t)w1 << 32) | w2;
		offset += 2;
	}

	return 0;
}
