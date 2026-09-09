// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include <iio/iio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "iio-config.h"
#include "xml-zstd.h"

#if WITH_ZSTD
#include <zstd.h>
#endif

void *iiod_get_xml_zstd(const struct iio_context *ctx, size_t *out_len)
{
	char *xml = iio_context_get_xml(ctx);
	size_t xml_len;
#if WITH_ZSTD
	size_t bound, ret;
	void *buf;
#endif

	if (!xml)
		return NULL;

	xml_len = strlen(xml);

#if WITH_ZSTD
	bound = ZSTD_compressBound(xml_len);

	buf = malloc(bound);
	if (!buf) {
		free(xml);
		return NULL;
	}

	ret = ZSTD_compress(buf, bound, xml, xml_len, 3);
	free(xml);

	if (ZSTD_isError(ret)) {
		IIO_WARNING("Unable to compress the XML string: %s\n", ZSTD_getErrorName(ret));
		free(buf);
		return NULL;
	}

	*out_len = ret;

	return buf;
#else
	*out_len = xml_len;

	return xml;
#endif
}
