/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NOOS_IIOD_H_
#define NOOS_IIOD_H_

/*
 * What a no-OS project calls to serve its devices with libiio's iiod.
 *
 * The project keeps its device table and transport settings with its other
 * init parameters (no-OS projects keep them in common_data.c); main() then
 * only calls noos_iiod_run().
 *
 * This header includes neither libiio's iio.h nor no-OS's iio_types.h,
 * which both define struct iio_device, so it can sit next to the no-OS
 * driver headers.
 */

#include <stdint.h>

struct noos_iio_device_info;
struct no_os_uart_init_param;
struct no_os_timer_init_param;
struct no_os_irq_init_param;
struct lwip_network_param;

/**
 * struct noos_iiod_timer - a hardware timer and its interrupt
 * @timer:        no-OS timer settings (id, freq_hz, platform_ops, extra);
 *                ticks_count is the period, where the user does not set it
 * @irq:          the interrupt controller it fires through
 * @irq_id:       the timer's interrupt number
 * @irq_handle:   the timer instance its callback is matched against
 * @irq_priority: interrupt priority. no-OS walks every timer callback with
 *                one shared iterator, so give all timers the same priority
 */
struct noos_iiod_timer {
	struct no_os_timer_init_param *timer;
	struct no_os_irq_init_param *irq;
	uint32_t irq_id;
	void *irq_handle;
	uint32_t irq_priority;
};

/**
 * struct noos_iiod_usb - the usb transport's settings
 * @tick:         timer that paces usb timeouts and keeps reads queued; its
 *                period is tick.timer->ticks_count / tick.timer->freq_hz
 * @vendor_id:    usb ids the device enumerates with; the host finds it by
 * @product_id:   its "IIO" interface, so any ids work
 * @manufacturer: usb strings shown by the host (iio_info -S)
 * @product:
 * @serial:
 */
struct noos_iiod_usb {
	struct noos_iiod_timer tick;
	uint16_t vendor_id;
	uint16_t product_id;
	const char *manufacturer;
	const char *product;
	const char *serial;
};

/**
 * struct noos_iiod_device - one row of the project's device table
 * @init:     bring the device up with @param (may be NULL)
 * @param:    the device's settings (may be NULL)
 * @get_info: fill the info the backend registers the device with
 *
 * A device that fails either step is logged and left out; the rest are
 * still served. An existing no-OS driver (iio_<part>.c) gets its row from
 * NOOS_IIOD_DRIVER_DEFINE() / NOOS_IIOD_DRIVER() in iio_noos_driver.h.
 */
struct noos_iiod_device {
	int (*init)(const void *param);
	const void *param;
	int (*get_info)(struct noos_iio_device_info *info);
};

/**
 * struct noos_iiod_param - what the project hands to noos_iiod_run()
 * @devices:    the device table
 * @nb_devices: number of rows in @devices
 * @uart:       uart transport settings (uart builds only)
 * @lwip:       lwIP and MAC/PHY settings (network builds only)
 * @usb:        usb settings (usb builds only)
 * @console:    uart the logs go to, on the usb and network builds (may be
 *              NULL). no-OS has no system console, so the project names
 *              one here instead of iiod picking a uart
 *
 * Only the fields of the transport built in are read; the others may be
 * NULL.
 */
struct noos_iiod_param {
	const struct noos_iiod_device *devices;
	unsigned int nb_devices;
	struct no_os_uart_init_param *uart;
	struct lwip_network_param *lwip;
	const struct noos_iiod_usb *usb;
	struct no_os_uart_init_param *console;
};

/*
 * Register the devices and run iiod on the transport chosen at build time.
 * Returns only on a setup error.
 */
int noos_iiod_run(const struct noos_iiod_param *param);

#endif /* NOOS_IIOD_H_ */
