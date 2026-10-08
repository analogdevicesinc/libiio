/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IIO_USB_HAL_H
#define IIO_USB_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IIO_USB_MAX_PIPES	4
#define IIO_USB_MAX_PACKET	512

enum iio_usb_event {
	IIO_USB_EVENT_CONNECTED,
	IIO_USB_EVENT_DISCONNECTED,
	IIO_USB_EVENT_RESET,
};

enum iio_usb_ctrl {
	IIO_USB_CTRL_RESET_PIPES,
	IIO_USB_CTRL_OPEN_PIPE,
	IIO_USB_CTRL_CLOSE_PIPE,
};

/* Used by the transports; cppcheck checks this header on its own. */
/* cppcheck-suppress-begin unusedStructMember */
struct iio_usb_dev_info {
	uint16_t	vendor_id;
	uint16_t	product_id;
	uint16_t	release;
	const char	*manufacturer;
	const char	*product;
	const char	*serial;
	const char	*interface_name;
	unsigned int	num_pipes;
};
/* cppcheck-suppress-end unusedStructMember */

struct iio_usb_ops {
	int (*init)(const struct iio_usb_dev_info *info);
	void (*remove)(void);
	int (*submit_rx)(unsigned int pipe, void *buf, uint32_t len);
	int (*submit_tx)(unsigned int pipe, const void *buf, uint32_t len);
	void (*cancel)(unsigned int pipe);
	void (*flush)(unsigned int pipe);
	uint16_t (*max_packet)(void);
};

extern const struct iio_usb_ops iio_usb_ops;

void iio_usb_on_rx(unsigned int pipe, uint32_t len, int status);
void iio_usb_on_tx(unsigned int pipe, uint32_t len, int status);
void iio_usb_on_event(unsigned int event);
void iio_usb_on_ctrl(unsigned int req, unsigned int pipe);

#endif /* IIO_USB_HAL_H */
