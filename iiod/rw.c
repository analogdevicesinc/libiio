// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2024 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "ops.h"

ssize_t write_all(struct parser_pdata *pdata, const void *src, size_t len)
{
	uintptr_t ptr = (uintptr_t) src;

	while (len) {
		ssize_t ret = pdata->writefd(pdata, (void *) ptr, len);
		if (ret < 0)
			return ret;
		if (!ret)
			return -EPIPE;
		ptr += ret;
		len -= ret;
	}

	return ptr - (uintptr_t) src;
}

ssize_t read_all(struct parser_pdata *pdata, void *dst, size_t len)
{
	uintptr_t ptr = (uintptr_t) dst;

	while (len) {
		ssize_t ret = pdata->readfd(pdata, (void *) ptr, len);
		if (ret < 0)
			return ret;
		if (!ret)
			return -EPIPE;
		ptr += ret;
		len -= ret;
	}

	return ptr - (uintptr_t) dst;
}
