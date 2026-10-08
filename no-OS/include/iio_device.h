/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NOOS_INCLUDE_IIO_DEVICE_H_
#define NOOS_INCLUDE_IIO_DEVICE_H_

#include <iio/iio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of IIO devices that can be registered */
#define NOOS_IIO_MAX_DEVICES 16

/*
 * Callback types: each takes the driver's own device instance as an opaque
 * void *.
 */
typedef int (*noos_iio_add_channels_t)(void *dev,
		struct iio_device *iio_device);

typedef int (*noos_iio_read_attr_t)(void *dev,
		const struct iio_device *iio_device,
		const struct iio_attr *attr,
		char *dst, size_t len);

typedef int (*noos_iio_write_attr_t)(void *dev,
		const struct iio_device *iio_device,
		const struct iio_attr *attr,
		const char *src, size_t len);

typedef int (*noos_iio_reg_read_t)(void *dev, uint32_t reg, uint32_t *val);
typedef int (*noos_iio_reg_write_t)(void *dev, uint32_t reg, uint32_t val);

/**
 * struct noos_iio_device_info - describes one IIO device for the backend
 * @name:          human-readable device name
 * @dev:           pointer to the no-OS device instance (opaque)
 * @add_channels:  populate channels on the iio_device (may be NULL)
 * @read_attr:     read an attribute value (may be NULL)
 * @write_attr:    write an attribute value (may be NULL)
 * @reg_read:      read a device register (may be NULL)
 * @reg_write:     write a device register (may be NULL)
 *
 * Fill it with a designated initializer so the fields left out are zero.
 */
struct noos_iio_device_info {
	const char *name;
	void *dev;
	noos_iio_add_channels_t add_channels;
	noos_iio_read_attr_t read_attr;
	noos_iio_write_attr_t write_attr;
	noos_iio_reg_read_t reg_read;
	noos_iio_reg_write_t reg_write;
};

/**
 * noos_iio_register_device() - register an IIO device before context creation
 * @info: pointer to a device info structure (contents are copied)
 *
 * Returns 0 on success, -EINVAL if @info is NULL, -ENOMEM if the device
 * table is full.
 */
int noos_iio_register_device(const struct noos_iio_device_info *info);

/**
 * noos_iiod_run() - bring up and run the IIOD server for the built transport
 *
 * Implemented by each transport; only the one selected at build time is
 * compiled. Returns a negative error code on setup failure.
 */
int noos_iiod_run(void);

/* Used by the transports; cppcheck checks this header on its own. */
/* cppcheck-suppress-begin unusedStructMember */
/* Chip-independent network configuration supplied by the platform. */
struct noos_net_config {
	const void *lwip_ops;	/* const struct no_os_lwip_ops * */
	void *mac_param;	/* chip-specific init parameter  */
	uint8_t mac_addr[6];
};
/* cppcheck-suppress-end unusedStructMember */

/* Accessed by the backend – not for direct application use */
extern struct noos_iio_device_info noos_iio_devices[];
extern unsigned int noos_iio_device_count;

#ifdef __cplusplus
}
#endif

#endif /* NOOS_INCLUDE_IIO_DEVICE_H_ */
