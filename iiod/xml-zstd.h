/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#ifndef __IIOD_XML_ZSTD_H__
#define __IIOD_XML_ZSTD_H__

#include <stddef.h>

struct iio_context;

/*
 * Largest PRINT payload a Libiio client will read; it sizes its receive buffer
 * up-front. A context whose served description exceeds this is truncated on
 * the client and fails to parse, so compressing it is not just an optimization.
 */
#define IIOD_MAX_XML_PAYLOAD 0x10000

/*
 * Serialize <ctx> into the buffer that the IIOD backends and
 * iiod_interpreter() expect, ZSTD-compressed if compression support was built
 * in. Stores the size in <out_len>. Returns NULL on failure; free the result
 * with free().
 */
void *iiod_get_xml_zstd(const struct iio_context *ctx, size_t *out_len);

#endif /* __IIOD_XML_ZSTD_H__ */
