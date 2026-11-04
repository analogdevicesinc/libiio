/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NOOS_IIO_DRIVER_DESC_H_
#define NOOS_IIO_DRIVER_DESC_H_

/*
 * What iio_noos_driver.c asks iio_noos_driver_desc.c. libiio and no-OS
 * both define struct iio_device, so they cannot share a file:
 * iio_noos_driver_desc.c reads the no-OS descriptor, iio_noos_driver.c
 * talks to libiio. Only plain types cross this header, so neither half
 * sees the other's struct iio_device.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "iio_noos_driver.h"

#define NOOS_DESC_ID_LEN 32

enum noos_desc_attr_kind {
	NOOS_DESC_ATTR_DEVICE,	/* descriptor->attributes */
	NOOS_DESC_ATTR_SHARED,	/* channel attrs shared by all: device level */
	NOOS_DESC_ATTR_DEBUG,	/* descriptor->debug_attributes */
	NOOS_DESC_ATTR_CHANNEL, /* channel attrs that stay on the channel */
};

struct noos_desc_chan {
	char id[NOOS_DESC_ID_LEN];
	bool output;
	bool scan_element;
	int scan_index;
	unsigned int bits;
	unsigned int length;
	unsigned int shift;
	bool is_signed;
	bool is_be;
};

unsigned int noos_desc_num_channels(const struct noos_iio_driver *a);
int noos_desc_get_chan(
		const struct noos_iio_driver *a, unsigned int ch, struct noos_desc_chan *out);

/* Name of the k-th attribute of a kind, NULL past the end. */
const char *noos_desc_attr_name(const struct noos_iio_driver *a, enum noos_desc_attr_kind kind,
		unsigned int ch, unsigned int k);
/* Call the attribute's show (write = false) or store; @ch for channels. */
int noos_desc_attr_rw(const struct noos_iio_driver *a, enum noos_desc_attr_kind kind,
		unsigned int ch, const char *name, char *buf, size_t len, bool write);

bool noos_desc_has_reg(const struct noos_iio_driver *a);
int noos_desc_reg_read(const struct noos_iio_driver *a, uint32_t reg, uint32_t *val);
int noos_desc_reg_write(const struct noos_iio_driver *a, uint32_t reg, uint32_t val);

/* Bytes per scan as no-OS lays the enabled channels out. */
size_t noos_desc_bytes_per_scan(const struct noos_iio_driver *a, uint32_t mask);
int noos_desc_enable(const struct noos_iio_driver *a, uint32_t mask, bool enable);

/* One whole transfer of @bytes through the driver's submit (or read_dev). */
int noos_desc_transfer(const struct noos_iio_driver *a, uint32_t mask, void *data, size_t bytes,
		bool output);

#endif /* NOOS_IIO_DRIVER_DESC_H_ */
