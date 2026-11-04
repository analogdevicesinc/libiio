/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NOOS_IIO_USB_H_
#define NOOS_IIO_USB_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NOOS_IIO_USB_MAX_PIPES 4
#define NOOS_IIO_USB_MAX_PACKET 512

enum noos_iio_usb_event {
	NOOS_IIO_USB_EVENT_CONNECTED,
	NOOS_IIO_USB_EVENT_DISCONNECTED,
	NOOS_IIO_USB_EVENT_RESET,
};

enum noos_iio_usb_ctrl {
	NOOS_IIO_USB_CTRL_RESET_PIPES,
	NOOS_IIO_USB_CTRL_OPEN_PIPE,
	NOOS_IIO_USB_CTRL_CLOSE_PIPE,
};

struct noos_iio_usb_dev_info {
	uint16_t vendor_id;
	uint16_t product_id;
	uint16_t release;
	const char *manufacturer;
	const char *product;
	const char *serial;
	const char *interface_name;
	unsigned int num_pipes;
};

struct noos_iio_usb_ops {
	int (*init)(const struct noos_iio_usb_dev_info *info);
	void (*remove)(void);
	int (*submit_rx)(unsigned int pipe, void *buf, uint32_t len);
	int (*submit_tx)(unsigned int pipe, const void *buf, uint32_t len);
	void (*cancel)(unsigned int pipe);
	void (*flush)(unsigned int pipe);
	uint16_t (*max_packet)(void);
};

extern const struct noos_iio_usb_ops noos_iio_usb_ops;

void noos_iio_usb_on_rx(unsigned int pipe, uint32_t len, int status);
void noos_iio_usb_on_tx(unsigned int pipe, uint32_t len, int status);
void noos_iio_usb_on_event(unsigned int event);
void noos_iio_usb_on_ctrl(unsigned int req, unsigned int pipe);

#endif /* NOOS_IIO_USB_H_ */
