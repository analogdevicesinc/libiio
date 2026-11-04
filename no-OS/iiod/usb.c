/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <no_os_irq.h>
#include <no_os_print_log.h>
#include <no_os_timer.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <tinyiiod/tinyiiod.h>

#include "iio_device.h"
#include "noos_iio_usb.h"
#include "noos_iiod.h"

#ifndef NOOS_IIO_USB_NUM_PIPES
#define NOOS_IIO_USB_NUM_PIPES 2
#endif

#define NOOS_IIO_USB_IFACE_NAME "IIO"

#define NOOS_IIO_USB_RX_RING_SIZE 2048
#define NOOS_IIO_USB_RX_RING_MASK (NOOS_IIO_USB_RX_RING_SIZE - 1)

/* Timeouts in usb_ticks, from the project's tick rate (usb_tick_init()). */
static uint32_t usb_io_timeout_ticks; /* 10 s */
/* How long a data pipe may hold a response back while pipe 0 has a command. */
static uint32_t usb_stall_ticks; /* 200 ms */

struct noos_iio_usb_pipe {
	volatile bool open;
	/* The host gave up on this data pipe (a cancelled stream): its
	 * commands are dropped and its responses fail until it is reopened. */
	volatile bool stalled;
	volatile uint32_t open_seq;
	volatile bool rx_pending;
	volatile int rx_err;
	volatile uint32_t rx_head;
	volatile uint32_t rx_tail;
	volatile bool tx_done;
	volatile int tx_err;
	volatile uint32_t tx_actual;
	uint8_t rx_chunk[NOOS_IIO_USB_MAX_PACKET] __attribute__((aligned(4)));
	uint8_t rx_ring[NOOS_IIO_USB_RX_RING_SIZE];
};

static struct noos_iio_usb_pipe pipes[NOOS_IIO_USB_NUM_PIPES];

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
static struct no_os_callback_desc tick_cb;
static uint32_t tick_irq_id;

/* Bytes waiting in a pipe's receive ring. */
static uint32_t ring_used(const struct noos_iio_usb_pipe *p)
{
	return p->rx_head - p->rx_tail;
}

/* Space left in a pipe's receive ring. */
static uint32_t ring_free(const struct noos_iio_usb_pipe *p)
{
	return NOOS_IIO_USB_RX_RING_SIZE - ring_used(p);
}

/* Copy a received USB packet into the ring (runs from the USB interrupt). */
static void ring_push(struct noos_iio_usb_pipe *p, const uint8_t *src, uint32_t len)
{
	uint32_t off = p->rx_head & NOOS_IIO_USB_RX_RING_MASK;
	uint32_t first = NOOS_IIO_USB_RX_RING_SIZE - off;

	if (first > len)
		first = len;

	memcpy(&p->rx_ring[off], src, first);
	if (len > first)
		memcpy(p->rx_ring, src + first, len - first);

	p->rx_head += len;
}

/*
 * Take up to len bytes out of the ring for the interpreter; returns how many.
 */
static uint32_t ring_pop(struct noos_iio_usb_pipe *p, uint8_t *dst, uint32_t len)
{
	uint32_t used = ring_used(p);
	uint32_t off, first;

	if (len > used)
		len = used;
	if (!len)
		return 0;

	off = p->rx_tail & NOOS_IIO_USB_RX_RING_MASK;
	first = NOOS_IIO_USB_RX_RING_SIZE - off;
	if (first > len)
		first = len;

	memcpy(dst, &p->rx_ring[off], first);
	if (len > first)
		memcpy(dst + first, p->rx_ring, len - first);

	p->rx_tail += len;

	return len;
}

/* Abort the queued USB transfers of one pipe. */
static void pipe_cancel(unsigned int index)
{
	struct noos_iio_usb_pipe *p = &pipes[index];

	noos_iio_usb_ops.cancel(index);

	p->rx_pending = false;
	p->tx_done = true;
}

/* Close every pipe, on a bus reset or when the host resets the pipes. */
static void pipes_close_all(void)
{
	unsigned int i;

	for (i = 0; i < NOOS_IIO_USB_NUM_PIPES; i++) {
		pipes[i].open = false;
		pipe_cancel(i);
	}
}

/* Platform callback, an OUT packet arrived: store it or keep the error. */
void noos_iio_usb_on_rx(unsigned int pipe, uint32_t len, int status)
{
	struct noos_iio_usb_pipe *p = &pipes[pipe];

	if (status)
		p->rx_err = status;
	else if (len)
		ring_push(p, p->rx_chunk, len);

	p->rx_pending = false;
}

/* Platform callback, an IN transfer finished: unblocks iiod_usb_write(). */
void noos_iio_usb_on_tx(unsigned int pipe, uint32_t len, int status)
{
	struct noos_iio_usb_pipe *p = &pipes[pipe];

	p->tx_actual = len;
	p->tx_err = status;
	p->tx_done = true;
}

/* Platform callback for configure/disconnect/reset: is the host usable? */
void noos_iio_usb_on_event(unsigned int event)
{
	switch (event) {
	case NOOS_IIO_USB_EVENT_CONNECTED:
		configured = 1;
		break;

	case NOOS_IIO_USB_EVENT_DISCONNECTED:
	case NOOS_IIO_USB_EVENT_RESET:
		configured = 0;
		pipes_close_all();
		break;

	default:
		break;
	}
}

/* libiio's vendor requests that open, close or reset the IIO pipes. */
void noos_iio_usb_on_ctrl(unsigned int req, unsigned int pipe)
{
	struct noos_iio_usb_pipe *p;

	if (pipe >= NOOS_IIO_USB_NUM_PIPES)
		return;

	p = &pipes[pipe];

	switch (req) {
	case NOOS_IIO_USB_CTRL_RESET_PIPES:
		pipes_close_all();
		break;

	case NOOS_IIO_USB_CTRL_OPEN_PIPE:
		/* A new session on this pipe: drop what the old one left. */
		pipe_cancel(pipe);
		p->rx_head = 0;
		p->rx_tail = 0;
		p->rx_err = 0;
		p->tx_err = 0;
		p->stalled = false;
		p->open_seq++;
		p->open = true;
		break;

	case NOOS_IIO_USB_CTRL_CLOSE_PIPE:
		p->open = false;
		pipe_cancel(pipe);
		break;

	default:
		break;
	}
}

/* Queue the next OUT read on a pipe if it is open and its ring has room. */
static void pipe_submit_rx(unsigned int index)
{
	struct noos_iio_usb_pipe *p = &pipes[index];
	uint16_t mps = noos_iio_usb_ops.max_packet();

	if (!configured || !p->open || p->stalled || p->rx_pending || p->rx_err)
		return;

	if (ring_free(p) < mps)
		return;

	p->rx_pending = true;

	if (noos_iio_usb_ops.submit_rx(index, p->rx_chunk, mps)) {
		p->rx_pending = false;
		p->rx_err = -EIO;
	}
}

/* Timer interrupt: advances the timeout clock and keeps every pipe reading. */
static void usb_tick_cb(void *ctx)
{
	unsigned int i;

	usb_ticks++;

	if (!configured)
		return;

	for (i = 0; i < NOOS_IIO_USB_NUM_PIPES; i++)
		pipe_submit_rx(i);
}

/* True while the host session that started the interpreter owns pipe 0. */
static bool session_alive(void)
{
	return configured && pipes[0].open && pipes[0].open_seq == session_seq;
}

/* True if the session is alive and this pipe is open. */
static bool pipe_usable(const struct noos_iio_usb_pipe *p)
{
	return session_alive() && p->open;
}

/*
 * Drop a data pipe the host no longer services, e.g. after it cancelled a
 * stream: whatever it still holds is discarded, and the session carries on
 * over pipe 0, where the host sends the commands that free the buffer.
 */
static void pipe_stall(unsigned int index)
{
	struct noos_iio_usb_pipe *p = &pipes[index];

	p->stalled = true;
	pipe_cancel(index);
	noos_iio_usb_ops.flush(index);

	p->rx_tail = p->rx_head;
	p->rx_err = 0;
}

/* Wait until an open pipe has data or an error to report, and pick it. */
static int pipe_wait_any(unsigned int *index)
{
	unsigned int i;

	while (session_alive()) {
		for (i = 0; i < NOOS_IIO_USB_NUM_PIPES; i++) {
			const struct noos_iio_usb_pipe *p = &pipes[i];

			if (!p->open || p->stalled)
				continue;

			/* An error on a data pipe ends that pipe, not the
			 * session. */
			if (i && p->rx_err) {
				pipe_stall(i);
				continue;
			}

			if (ring_used(p) || p->rx_err) {
				*index = i;
				return 0;
			}
		}
	}

	return -ENODEV;
}

/*
 * Interpreter read: take the pipe that has a new command, then read the rest
 * of that command from the same pipe, with a timeout.
 */
static ssize_t iiod_usb_read(struct iiod_pdata *pdata, void *buf, size_t size)
{
	uint8_t *dst = (uint8_t *)buf;
	struct noos_iio_usb_pipe *p;
	bool fresh = false;
	uint32_t start;
	size_t total = 0;
	int ret;

again:
	if (!pipe_locked) {
		ret = pipe_wait_any(&cur_pipe);
		if (ret)
			return ret;

		pipe_locked = true;
		fresh = true;
	}

	p = &pipes[cur_pipe];
	start = usb_ticks;

	while (total < size) {
		uint32_t n;

		if (!pipe_usable(p) || p->rx_err) {
			int err = pipe_usable(p) ? p->rx_err : -ENODEV;

			/* Nothing of the command consumed yet: a data pipe
			 * going away drops it, and the next command may come
			 * from another pipe. */
			if (fresh && cur_pipe && !total && session_alive()) {
				pipe_stall(cur_pipe);
				pipe_locked = false;
				goto again;
			}

			p->rx_err = 0;
			return err;
		}

		n = ring_pop(p, dst + total, (uint32_t)(size - total));
		if (n) {
			total += n;
			start = usb_ticks;
			continue;
		}

		if (usb_ticks - start >= usb_io_timeout_ticks)
			return -ETIMEDOUT;
	}

	return (ssize_t)size;
}

/*
 * Interpreter write: send the reply on the command's pipe; give up early if
 * the host abandoned that pipe.
 */
static ssize_t iiod_usb_write(struct iiod_pdata *pdata, const void *buf, size_t size)
{
	unsigned int index = cur_pipe;
	struct noos_iio_usb_pipe *p = &pipes[index];
	uint32_t total = 0;

	/* The response ends the command; the next read may pick any pipe. */
	pipe_locked = false;

	if (p->stalled)
		return -EPIPE;

	while (total < size) {
		uint32_t start;

		if (!pipe_usable(p))
			return -ENODEV;

		p->tx_done = false;
		p->tx_err = 0;
		p->tx_actual = 0;

		if (noos_iio_usb_ops.submit_tx(index, (const uint8_t *)buf + total, size - total)) {
			p->tx_done = true;
			return -EIO;
		}

		start = usb_ticks;

		while (!p->tx_done) {
			if (!pipe_usable(p)) {
				pipe_cancel(index);
				return -ENODEV;
			}

			/* A host that cancelled the stream stops reading the
			 * data pipe, and sends the teardown on pipe 0: serve
			 * that instead of waiting out the full timeout. */
			if (index && ring_used(&pipes[0]) && usb_ticks - start >= usb_stall_ticks) {
				pipe_stall(index);
				return -EPIPE;
			}

			if (usb_ticks - start >= usb_io_timeout_ticks) {
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

	for (i = 0; i < NOOS_IIO_USB_NUM_PIPES; i++) {
		struct noos_iio_usb_pipe *p = &pipes[i];

		/* The host reopened pipe 0 meanwhile: that is the next session. */
		if (i == 0 && p->open_seq != session_seq)
			continue;

		p->open = false;
		pipe_cancel(i);
		noos_iio_usb_ops.flush(i);

		p->rx_head = 0;
		p->rx_tail = 0;
		p->rx_err = 0;
		p->tx_err = 0;
		p->stalled = false;
	}
}

/* Start the project's tick timer, which drives timeouts and RX re-arming. */
static int usb_tick_init(const struct noos_iiod_timer *tick)
{
	uint32_t tick_hz;
	int ret;

	if (!tick->timer || !tick->irq || !tick->timer->ticks_count)
		return -EINVAL;

	tick_hz = tick->timer->freq_hz / tick->timer->ticks_count;
	if (tick_hz < 5)
		return -EINVAL;
	usb_io_timeout_ticks = tick_hz * 10;
	usb_stall_ticks = tick_hz / 5;

	tick_cb = (struct no_os_callback_desc){
		.callback = usb_tick_cb,
		.event = NO_OS_EVT_TIM_ELAPSED,
		.peripheral = NO_OS_TIM_IRQ,
		.handle = tick->irq_handle,
	};
	tick_irq_id = tick->irq_id;

	ret = no_os_timer_init(&tick_timer, tick->timer);
	if (ret)
		return ret;

	ret = no_os_irq_ctrl_init(&tick_irq, tick->irq);
	if (ret)
		goto remove_timer;

	ret = no_os_irq_register_callback(tick_irq, tick_irq_id, &tick_cb);
	if (ret)
		goto remove_irq;

	ret = no_os_irq_set_priority(tick_irq, tick_irq_id, tick->irq_priority);
	if (ret)
		goto unregister_cb;

	ret = no_os_irq_enable(tick_irq, tick_irq_id);
	if (ret)
		goto unregister_cb;

	ret = no_os_timer_start(tick_timer);
	if (ret)
		goto disable_irq;

	return 0;

disable_irq:
	no_os_irq_disable(tick_irq, tick_irq_id);
unregister_cb:
	no_os_irq_unregister_callback(tick_irq, tick_irq_id, &tick_cb);
remove_irq:
	no_os_irq_ctrl_remove(tick_irq);
	tick_irq = NULL;
remove_timer:
	no_os_timer_remove(tick_timer);
	tick_timer = NULL;

	return ret;
}

/*
 * Undo usb_tick_init(), last step first. The callback is unregistered with
 * interrupts off, so the timer cannot fire into USB state being torn down.
 */
static void usb_tick_remove(void)
{
	no_os_timer_stop(tick_timer);
	no_os_irq_disable(tick_irq, tick_irq_id);
	no_os_irq_global_disable(tick_irq);
	no_os_irq_unregister_callback(tick_irq, tick_irq_id, &tick_cb);
	no_os_irq_global_enable(tick_irq);
	no_os_irq_ctrl_remove(tick_irq);
	tick_irq = NULL;
	no_os_timer_remove(tick_timer);
	tick_timer = NULL;
}

/*
 * USB server: bring up the device, build the XML, then run one interpreter
 * per host session.
 */
int iiod_usb_run(const struct noos_iiod_usb *usb)
{
	const struct noos_iio_usb_dev_info dev_info = {
		.vendor_id = usb->vendor_id,
		.product_id = usb->product_id,
		.release = 0x0100,
		.manufacturer = usb->manufacturer,
		.product = usb->product,
		.serial = usb->serial,
		.interface_name = NOOS_IIO_USB_IFACE_NAME,
		.num_pipes = NOOS_IIO_USB_NUM_PIPES,
	};
	struct iio_context_params ctx_params = { 0 };
	struct iio_context *ctx;
	char *xml;
	size_t xml_len;
	int ret;

	configured = 0;

	ret = noos_iio_usb_ops.init(&dev_info);
	if (ret)
		return ret;

	ret = usb_tick_init(&usb->tick);
	if (ret) {
		pr_err("USB tick timer init failed: %d\n", ret);
		goto remove_usb;
	}

	pr_info("USB initialized, waiting for host...\n");

	ret = iiod_init();
	if (ret < 0) {
		pr_err("iiod_init failed: %d\n", ret);
		goto remove_tick;
	}

	ctx = iio_create_context(&ctx_params, "no-os:");
	ret = iio_err(ctx);
	if (ret) {
		pr_err("iio_create_context failed: %d\n", ret);
		goto cleanup_iiod;
	}

	xml = iio_context_get_xml(ctx);
	if (!xml) {
		pr_err("iio_context_get_xml failed\n");
		ret = -ENOMEM;
		goto destroy_ctx;
	}

	xml_len = strlen(xml) + 1;

	pr_info("IIO context ready (%u bytes XML)\n", (unsigned int)xml_len);

	while (1) {
		/* A session starts when the host opens pipe 0. */
		while (!configured || !pipes[0].open)
			;

		session_seq = pipes[0].open_seq;
		cur_pipe = 0;
		pipe_locked = false;

		pr_info("USB: session started\n");

		iiod_interpreter(ctx, (struct iiod_pdata *)pipes, iiod_usb_read, iiod_usb_write,
				xml, xml_len);

		pr_info("USB: session ended\n");

		session_close();
	}

	free(xml);
destroy_ctx:
	iio_context_destroy(ctx);
cleanup_iiod:
	iiod_cleanup();
remove_tick:
	usb_tick_remove();
remove_usb:
	noos_iio_usb_ops.remove();

	return ret;
}

/* Entry point of the usb build: register the project's devices, IIOD over USB. */
int noos_iiod_run(const struct noos_iiod_param *param)
{
	if (!param || !param->usb)
		return -EINVAL;

	noos_iiod_register(param);

	return iiod_usb_run(param->usb);
}
