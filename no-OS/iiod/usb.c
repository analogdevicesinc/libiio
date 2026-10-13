/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <no_os_uart.h>
#include <no_os_print_log.h>
#include <no_os_timer.h>
#include <no_os_irq.h>
#include <tinyiiod/tinyiiod.h>
#include "parameters.h"
#include "iio_device.h"
#include "iio_usb_hal.h"

#ifndef IIO_USB_NUM_PIPES
#define IIO_USB_NUM_PIPES	2
#endif

#ifndef NO_OS_USB_VENDOR_ID
#define NO_OS_USB_VENDOR_ID	0x0456
#endif

#ifndef NO_OS_USB_PRODUCT_ID
#define NO_OS_USB_PRODUCT_ID	0xB673
#endif

#ifndef NO_OS_USB_MANUFACTURER
#define NO_OS_USB_MANUFACTURER	"Analog Devices"
#endif

#ifndef NO_OS_USB_PRODUCT
#define NO_OS_USB_PRODUCT	"IIO"
#endif

#ifndef NO_OS_USB_SERIAL
#define NO_OS_USB_SERIAL	"000000001"
#endif

#define IIO_USB_IFACE_NAME	"IIO"

#define IIO_USB_RX_RING_SIZE	2048
#define IIO_USB_RX_RING_MASK	(IIO_USB_RX_RING_SIZE - 1)

#define USB_TICK_HZ		(USB_TICK_TIMER_FREQ_HZ / USB_TICK_TIMER_TICKS)
#define USB_IO_TIMEOUT_TICKS	(USB_TICK_HZ * 10)

struct iio_usb_pipe {
	volatile bool		open;
	volatile uint32_t	open_seq;
	volatile bool		rx_pending;
	volatile int		rx_err;
	volatile uint32_t	rx_head;
	volatile uint32_t	rx_tail;
	volatile bool		tx_done;
	volatile int		tx_err;
	volatile uint32_t	tx_actual;
	uint8_t			rx_chunk[IIO_USB_MAX_PACKET]
	__attribute__((aligned(4)));
	uint8_t			rx_ring[IIO_USB_RX_RING_SIZE];
};

static struct iio_usb_pipe pipes[IIO_USB_NUM_PIPES];

static volatile int configured;
static volatile uint32_t usb_ticks;

/*
 * One interpreter serves every pipe. A read with no command in progress waits
 * on all open pipes and takes the first one with data; the rest of that
 * command (payload reads and the response) then stays on the same pipe until
 * the response is written.
 */
static unsigned int cur_pipe;
static bool pipe_locked;
static uint32_t session_seq;

static struct no_os_timer_desc *tick_timer;
static struct no_os_irq_ctrl_desc *tick_irq;

static uint32_t ring_used(const struct iio_usb_pipe *p)
{
	return p->rx_head - p->rx_tail;
}

static uint32_t ring_free(const struct iio_usb_pipe *p)
{
	return IIO_USB_RX_RING_SIZE - ring_used(p);
}

static void ring_push(struct iio_usb_pipe *p, const uint8_t *src, uint32_t len)
{
	uint32_t off = p->rx_head & IIO_USB_RX_RING_MASK;
	uint32_t first = IIO_USB_RX_RING_SIZE - off;

	if (first > len)
		first = len;

	memcpy(&p->rx_ring[off], src, first); /* Flawfinder: ignore */
	if (len > first)
		memcpy(p->rx_ring, src + first, len - first); /* Flawfinder: ignore */

	p->rx_head += len;
}

static uint32_t ring_pop(struct iio_usb_pipe *p, uint8_t *dst, uint32_t len)
{
	uint32_t used = ring_used(p);
	uint32_t off, first;

	if (len > used)
		len = used;
	if (!len)
		return 0;

	off = p->rx_tail & IIO_USB_RX_RING_MASK;
	first = IIO_USB_RX_RING_SIZE - off;
	if (first > len)
		first = len;

	memcpy(dst, &p->rx_ring[off], first); /* Flawfinder: ignore */
	if (len > first)
		memcpy(dst + first, p->rx_ring, len - first); /* Flawfinder: ignore */

	p->rx_tail += len;

	return len;
}

static void pipe_cancel(unsigned int index)
{
	struct iio_usb_pipe *p = &pipes[index];

	iio_usb_ops.cancel(index);

	p->rx_pending = false;
	p->tx_done = true;
}

static void pipes_close_all(void)
{
	unsigned int i;

	for (i = 0; i < IIO_USB_NUM_PIPES; i++) {
		pipes[i].open = false;
		pipe_cancel(i);
	}
}

void iio_usb_on_rx(unsigned int pipe, uint32_t len, int status)
{
	struct iio_usb_pipe *p = &pipes[pipe];

	if (status)
		p->rx_err = status;
	else if (len)
		ring_push(p, p->rx_chunk, len);

	p->rx_pending = false;
}

void iio_usb_on_tx(unsigned int pipe, uint32_t len, int status)
{
	struct iio_usb_pipe *p = &pipes[pipe];

	p->tx_actual = len;
	p->tx_err = status;
	p->tx_done = true;
}

void iio_usb_on_event(unsigned int event)
{
	switch (event) {
	case IIO_USB_EVENT_CONNECTED:
		configured = 1;
		break;

	case IIO_USB_EVENT_DISCONNECTED:
	case IIO_USB_EVENT_RESET:
		configured = 0;
		pipes_close_all();
		break;

	default:
		break;
	}
}

void iio_usb_on_ctrl(unsigned int req, unsigned int pipe)
{
	struct iio_usb_pipe *p;

	if (pipe >= IIO_USB_NUM_PIPES)
		return;

	p = &pipes[pipe];

	switch (req) {
	case IIO_USB_CTRL_RESET_PIPES:
		pipes_close_all();
		break;

	case IIO_USB_CTRL_OPEN_PIPE:
		/* A new session on this pipe: drop what the old one left. */
		pipe_cancel(pipe);
		p->rx_head = 0;
		p->rx_tail = 0;
		p->rx_err = 0;
		p->tx_err = 0;
		p->open_seq++;
		p->open = true;
		break;

	case IIO_USB_CTRL_CLOSE_PIPE:
		p->open = false;
		pipe_cancel(pipe);
		break;

	default:
		break;
	}
}

static void pipe_submit_rx(unsigned int index)
{
	struct iio_usb_pipe *p = &pipes[index];
	uint16_t mps = iio_usb_ops.max_packet();

	if (!configured || !p->open || p->rx_pending || p->rx_err)
		return;

	if (ring_free(p) < mps)
		return;

	p->rx_pending = true;

	if (iio_usb_ops.submit_rx(index, p->rx_chunk, mps)) {
		p->rx_pending = false;
		p->rx_err = -EIO;
	}
}

static void usb_tick_cb(void *ctx)
{
	unsigned int i;

	usb_ticks++;

	if (!configured)
		return;

	for (i = 0; i < IIO_USB_NUM_PIPES; i++)
		pipe_submit_rx(i);
}

static bool session_alive(void)
{
	return configured && pipes[0].open && pipes[0].open_seq == session_seq;
}

static bool pipe_usable(const struct iio_usb_pipe *p)
{
	return session_alive() && p->open;
}

/* Wait until an open pipe has data or an error to report, and pick it. */
static int pipe_wait_any(unsigned int *index)
{
	unsigned int i;

	while (session_alive()) {
		for (i = 0; i < IIO_USB_NUM_PIPES; i++) {
			const struct iio_usb_pipe *p = &pipes[i];

			if (!p->open)
				continue;

			if (ring_used(p) || p->rx_err) {
				*index = i;
				return 0;
			}
		}
	}

	return -ENODEV;
}

static ssize_t iiod_usb_read(struct iiod_pdata *pdata, void *buf, size_t size)
{
	uint8_t *dst = (uint8_t *)buf;
	struct iio_usb_pipe *p;
	uint32_t start;
	size_t total = 0;
	int ret;

	if (!pipe_locked) {
		ret = pipe_wait_any(&cur_pipe);
		if (ret)
			return ret;

		pipe_locked = true;
	}

	p = &pipes[cur_pipe];
	start = usb_ticks;

	while (total < size) {
		uint32_t n;

		if (!pipe_usable(p) || p->rx_err) {
			int err = pipe_usable(p) ? p->rx_err : -ENODEV;

			p->rx_err = 0;
			return err;
		}

		n = ring_pop(p, dst + total, (uint32_t)(size - total));
		if (n) {
			total += n;
			start = usb_ticks;
			continue;
		}

		if (usb_ticks - start >= USB_IO_TIMEOUT_TICKS)
			return -ETIMEDOUT;
	}

	return (ssize_t)size;
}

static ssize_t iiod_usb_write(struct iiod_pdata *pdata, const void *buf,
			      size_t size)
{
	unsigned int index = cur_pipe;
	struct iio_usb_pipe *p = &pipes[index];
	uint32_t total = 0;

	/* The response ends the command; the next read may pick any pipe. */
	pipe_locked = false;

	while (total < size) {
		uint32_t start;

		if (!pipe_usable(p))
			return -ENODEV;

		p->tx_done = false;
		p->tx_err = 0;
		p->tx_actual = 0;

		if (iio_usb_ops.submit_tx(index, (const uint8_t *)buf + total,
					  size - total)) {
			p->tx_done = true;
			return -EIO;
		}

		start = usb_ticks;

		while (!p->tx_done) {
			if (!pipe_usable(p)) {
				pipe_cancel(index);
				return -ENODEV;
			}

			if (usb_ticks - start >= USB_IO_TIMEOUT_TICKS) {
				pipe_cancel(index);
				return -ETIMEDOUT;
			}
		}

		if (p->tx_err)
			return -EIO;
		if (!p->tx_actual)
			return -ENODEV;

		total += p->tx_actual;
	}

	return (ssize_t)size;
}

/* Close every pipe the finished session still holds and drop their data. */
static void session_close(void)
{
	unsigned int i;

	for (i = 0; i < IIO_USB_NUM_PIPES; i++) {
		struct iio_usb_pipe *p = &pipes[i];

		/* The host reopened pipe 0 meanwhile: that is the next session. */
		if (i == 0 && p->open_seq != session_seq)
			continue;

		p->open = false;
		pipe_cancel(i);
		iio_usb_ops.flush(i);

		p->rx_head = 0;
		p->rx_tail = 0;
		p->rx_err = 0;
		p->tx_err = 0;
	}
}

static int usb_tick_init(void)
{
	struct no_os_timer_init_param tick_tip = {
		.id = USB_TICK_TIMER_ID,
		.freq_hz = USB_TICK_TIMER_FREQ_HZ,
		.ticks_count = USB_TICK_TIMER_TICKS,
		.platform_ops = USB_TICK_TIMER_OPS,
		.extra = USB_TICK_TIMER_EXTRA,
	};
	struct no_os_irq_init_param tick_irq_ip = {
		.irq_ctrl_id = 0,
		.platform_ops = USB_TICK_IRQ_OPS,
		.extra = NULL,
	};
	struct no_os_callback_desc tick_cb = {
		.callback = usb_tick_cb,
		.ctx = NULL,
		.event = NO_OS_EVT_TIM_ELAPSED,
		.peripheral = NO_OS_TIM_IRQ,
		.handle = USB_TICK_IRQ_HANDLE,
	};
	int ret;

	ret = no_os_timer_init(&tick_timer, &tick_tip);
	if (ret)
		return ret;

	ret = no_os_irq_ctrl_init(&tick_irq, &tick_irq_ip);
	if (ret)
		goto remove_timer;

	ret = no_os_irq_register_callback(tick_irq, USB_TICK_IRQ_ID, &tick_cb);
	if (ret)
		goto remove_irq;

	ret = no_os_irq_set_priority(tick_irq, USB_TICK_IRQ_ID,
				     USB_TICK_IRQ_PRIORITY);
	if (ret)
		goto remove_irq;

	ret = no_os_irq_enable(tick_irq, USB_TICK_IRQ_ID);
	if (ret)
		goto remove_irq;

	return no_os_timer_start(tick_timer);

remove_irq:
	no_os_irq_ctrl_remove(tick_irq);
	tick_irq = NULL;
remove_timer:
	no_os_timer_remove(tick_timer);
	tick_timer = NULL;

	return ret;
}

int iiod_usb_run(void)
{
	static const struct iio_usb_dev_info dev_info = {
		.vendor_id	= NO_OS_USB_VENDOR_ID,
		.product_id	= NO_OS_USB_PRODUCT_ID,
		.release	= 0x0100,
		.manufacturer	= NO_OS_USB_MANUFACTURER,
		.product	= NO_OS_USB_PRODUCT,
		.serial		= NO_OS_USB_SERIAL,
		.interface_name	= IIO_USB_IFACE_NAME,
		.num_pipes	= IIO_USB_NUM_PIPES,
	};
	struct iio_context_params ctx_params = {0};
	struct iio_context *ctx;
	char *xml;
	size_t xml_len;
	int ret;

	configured = 0;

	ret = iio_usb_ops.init(&dev_info);
	if (ret)
		return ret;

	ret = usb_tick_init();
	if (ret) {
		pr_err("USB tick timer init failed: %d\n", ret);
		goto remove_usb;
	}

	pr_info("USB initialized, waiting for host...\n");

	ret = iiod_init();
	if (ret < 0) {
		pr_err("iiod_init failed: %d\n", ret);
		goto remove_usb;
	}

	ctx = iio_create_context(&ctx_params, "no-os:");
	if (iio_err(ctx)) {
		pr_err("iio_create_context failed\n");
		iiod_cleanup();
		ret = -1;
		goto remove_usb;
	}

	xml = iio_context_get_xml(ctx);
	if (!xml) {
		pr_err("iio_context_get_xml failed\n");
		iio_context_destroy(ctx);
		iiod_cleanup();
		ret = -1;
		goto remove_usb;
	}

	xml_len = strlen(xml) + 1; /* Flawfinder: ignore */

	pr_info("IIO context ready (%u bytes XML)\n", (unsigned int)xml_len);

	while (1) {
		/* A session starts when the host opens pipe 0. */
		while (!configured || !pipes[0].open)
			;

		session_seq = pipes[0].open_seq;
		cur_pipe = 0;
		pipe_locked = false;

		pr_info("USB: session started\n");

		iiod_interpreter(ctx, (struct iiod_pdata *)pipes,
				 iiod_usb_read, iiod_usb_write, xml, xml_len);

		pr_info("USB: session ended\n");

		session_close();
	}

remove_usb:
	iio_usb_ops.remove();

	return ret;
}

int noos_iiod_run(void)
{
	struct no_os_uart_desc *console;
	struct no_os_uart_init_param console_ip = {
		.device_id = UART_DEVICE_ID,
		.baud_rate = UART_BAUDRATE,
		.size = NO_OS_UART_CS_8,
		.parity = NO_OS_UART_PAR_NO,
		.stop = NO_OS_UART_STOP_1_BIT,
		.platform_ops = UART_OPS,
		.extra = UART_EXTRA,
	};
	int ret;

	ret = no_os_uart_init(&console, &console_ip);
	if (ret)
		return ret;

	no_os_uart_stdio(console);

	ret = iiod_usb_run();

	no_os_uart_remove(console);

	return ret;
}
