/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2024 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#ifndef __LIBTINYIIOD_H__
#define __LIBTINYIIOD_H__

#include <iio/iio.h>

struct iiod_pdata;

/* Default Libiio context parameters for IIOD.
 * You can modify it if needed before calling iiod_interpreter(). */
extern struct iio_context_params iiod_params;

/* Execute the IIOD interpreter using the specified read_cb/write_cb callbacks.
 * IIOD will run until one of the callbacks returns a negative error code.
 *
 * ctx: Libiio context to use
 * pdata: User-provided data structure (or NULL) that will be passed along to
 *   the read_cb/write_cb callbacks
 * read_cb: Callback to read from the bus. This can block, and the application
 *   is free to do any unrelated processing in this callback.
 * write_cb: Callback to write data to the bus.
 * xml: XML representation of the Libiio context. It is recommended to pass a
 *   ZSTD-compressed buffer here, as it would be much faster to transfer on the
 *   bus; but a plain XML string works as well.
 * xml_len: Size of the ZSTD-compressed data, or length of the XML string.
 */
int iiod_interpreter(struct iio_context *ctx,
		     struct iiod_pdata *pdata,
		     ssize_t (*read_cb)(struct iiod_pdata *, void *, size_t),
		     ssize_t (*write_cb)(struct iiod_pdata *, const void *, size_t),
		     const void *xml, size_t xml_len);

#endif /* __LIBTINYIIOD_H__ */
