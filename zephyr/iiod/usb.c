/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <tinyiiod/tinyiiod.h>
#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/drivers/usb/udc.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define DT_DRV_COMPAT adi_iio_usb

LOG_MODULE_REGISTER(iiod_usb, CONFIG_LIBIIO_LOG_LEVEL);

#define IIO_USB_MAX_PIPES 6

/* Vendor control request commands (must match host-side libiio usb.c) */
#define IIO_USD_CMD_RESET_PIPES 0
#define IIO_USD_CMD_OPEN_PIPE   1
#define IIO_USD_CMD_CLOSE_PIPE  2

/* UDC buffer metadata extended with the session a buffer belongs to. The
 * udc_buf_info must come first so the stack can still use the buffer unchanged.
 */
struct iio_usb_buf_info {
	struct udc_buf_info udc;
	uint32_t session;
} __packed;

/*
 * IIO USB class descriptor structure.
 *
 * Single interface with num-pipes bulk IN/OUT endpoint pairs.
 * The host-side libiio (usb.c) identifies IIO devices by scanning
 * for interfaces whose iInterface string descriptor is "IIO".
 * Endpoint ordering: IN before OUT — the host's usb_verify_eps()
 * expects even-indexed endpoints to be IN, odd-indexed to be OUT.
 */
struct iio_usb_desc {
	struct usb_if_descriptor if0;
	struct usb_ep_descriptor if0_ep[IIO_USB_MAX_PIPES * 2];    /* FS endpoints */
	struct usb_ep_descriptor if0_hs_ep[IIO_USB_MAX_PIPES * 2]; /* HS endpoints */
	struct usb_desc_header nil_desc;
};

/*
 * Build the descriptor initializer with endpoint pairs for each pipe.
 * Each pipe N gets: IN endpoint 0x81+N, OUT endpoint 0x01+N
 */
#define EP_FS_IN(n)                                                                                \
	[n * 2] = {                                                                                \
		.bLength = sizeof(struct usb_ep_descriptor),                                       \
		.bDescriptorType = USB_DESC_ENDPOINT,                                              \
		.bEndpointAddress = 0x81 + n,                                                      \
		.bmAttributes = USB_EP_TYPE_BULK,                                                  \
		.wMaxPacketSize = sys_cpu_to_le16(64U),                                            \
		.bInterval = 0x00,                                                                 \
	}

#define EP_FS_OUT(n)                                                                               \
	[n * 2 + 1] = {                                                                            \
		.bLength = sizeof(struct usb_ep_descriptor),                                       \
		.bDescriptorType = USB_DESC_ENDPOINT,                                              \
		.bEndpointAddress = 0x01 + n,                                                      \
		.bmAttributes = USB_EP_TYPE_BULK,                                                  \
		.wMaxPacketSize = sys_cpu_to_le16(64U),                                            \
		.bInterval = 0x00,                                                                 \
	}

#define EP_HS_IN(n)                                                                                \
	[n * 2] = {                                                                                \
		.bLength = sizeof(struct usb_ep_descriptor),                                       \
		.bDescriptorType = USB_DESC_ENDPOINT,                                              \
		.bEndpointAddress = 0x81 + n,                                                      \
		.bmAttributes = USB_EP_TYPE_BULK,                                                  \
		.wMaxPacketSize = sys_cpu_to_le16(512U),                                           \
		.bInterval = 0x00,                                                                 \
	}

#define EP_HS_OUT(n)                                                                               \
	[n * 2 + 1] = {                                                                            \
		.bLength = sizeof(struct usb_ep_descriptor),                                       \
		.bDescriptorType = USB_DESC_ENDPOINT,                                              \
		.bEndpointAddress = 0x01 + n,                                                      \
		.bmAttributes = USB_EP_TYPE_BULK,                                                  \
		.wMaxPacketSize = sys_cpu_to_le16(512U),                                           \
		.bInterval = 0x00,                                                                 \
	}

#define EP_FS_PAIR(n) EP_FS_IN(n), EP_FS_OUT(n)
#define EP_HS_PAIR(n) EP_HS_IN(n), EP_HS_OUT(n)

#define DECLARE_EP_FS_PAIR(n, _) EP_FS_PAIR(n)
#define DECLARE_EP_HS_PAIR(n, _) EP_HS_PAIR(n)

/*
 * Per-pipe state. Received packets reach the interpreter as whole net_bufs,
 * each stamped with the session it arrived for.
 */
struct iio_usb_pipe {
	uint8_t idx;
	/* Receive buffers handed over by the completion handler, oldest first */
	struct k_fifo rx_fifo;
	/* Signalled when rx_fifo or rx_err changed; never reset */
	struct k_sem rx_sem;
	/* Bounds a pipe to rx-depth receive buffers */
	struct k_sem rx_credit;
	struct k_sem tx_sem;
	/* Hands this pipe's thread a session. Latches, so a reopen arriving
	 * while the previous interpreter unwinds is not lost.
	 */
	struct k_sem start_sem;
	/* Bumped at each session start; anything already queued keeps the old stamp */
	atomic_t session;
	/* Receive error, stamped with the session it ended so it cannot outlive it */
	int rx_err;
	atomic_t rx_err_session;
	/* Session this pipe's interpreter is serving. Both directions compare
	 * against it. Touched only by this pipe's thread.
	 */
	uint32_t serving;
	struct net_buf *rx_cur;
	uint16_t rx_cur_off;
	int tx_err;
	/* Set by the control handler, read by this pipe's thread and by the
	 * completion handler, so atomic rather than a plain bool.
	 */
	atomic_t open;
	struct iio_usb_data *data; /* back-pointer to parent */
};

struct iio_usb_data {
	struct iio_usb_desc *const desc;
	const struct usb_desc_header *const *const fs_desc;
	const struct usb_desc_header *const *const hs_desc;
	struct usbd_desc_node *const iface_str_desc;
	struct usbd_class_data *c_data;
	/* Given when .init sets up the pipes; statically initialised because
	 * .init may run long after boot, on an app-supplied USB device.
	 */
	struct k_sem init_sem;
	uint8_t num_pipes;
	uint16_t tx_buf_size;
	uint8_t rx_depth;
	struct net_buf_pool *rx_pool;
	struct net_buf_pool *tx_pool;
	struct iio_usb_pipe pipes[IIO_USB_MAX_PIPES];
	bool enabled;

	/* Pipe thread infrastructure */
	struct k_thread *pipe_threads;
	k_thread_stack_t *pipe_stacks;

	/* Shared IIO context and XML for the pipe interpreters */
	struct iio_context *ctx;
	const void *xml;
	size_t xml_len;
};

/*
 * Resolve a pipe's endpoint descriptor for the speed the bus is running at.
 * These cannot be cached: .init runs once per registered speed and sees the
 * other speed's array still holding placeholders, and the two may differ.
 */
static const struct usb_ep_descriptor *iio_usb_ep_desc(struct usbd_class_data *const c_data,
						       const unsigned int desc_idx)
{
	struct iio_usb_data *data = usbd_class_get_private(c_data);
	struct iio_usb_desc *desc = data->desc;

	if (USBD_SUPPORTS_HIGH_SPEED &&
	    usbd_bus_speed(usbd_class_get_ctx(c_data)) == USBD_SPEED_HS) {
		return &desc->if0_hs_ep[desc_idx];
	}

	return &desc->if0_ep[desc_idx];
}

static uint8_t iio_usb_get_bulk_in(struct usbd_class_data *const c_data,
				   const unsigned int pipe_idx)
{
	return iio_usb_ep_desc(c_data, pipe_idx * 2)->bEndpointAddress;
}

static uint8_t iio_usb_get_bulk_out(struct usbd_class_data *const c_data,
				    const unsigned int pipe_idx)
{
	return iio_usb_ep_desc(c_data, pipe_idx * 2 + 1)->bEndpointAddress;
}

/* The stack writes the max packet size it settled on back into the descriptor */
static uint16_t iio_usb_get_bulk_mps(struct usbd_class_data *const c_data,
				     const unsigned int desc_idx)
{
	return sys_le16_to_cpu(iio_usb_ep_desc(c_data, desc_idx)->wMaxPacketSize);
}

/*
 * Find the pipe that owns a given endpoint address.
 * Returns the pipe index or -1 if not found.
 */
static int find_pipe_by_ep(struct usbd_class_data *const c_data, const uint8_t ep_addr)
{
	struct iio_usb_data *data = usbd_class_get_private(c_data);

	for (int i = 0; i < data->num_pipes; i++) {
		if (iio_usb_get_bulk_in(c_data, i) == ep_addr ||
		    iio_usb_get_bulk_out(c_data, i) == ep_addr) {
			return i;
		}
	}

	return -1;
}

/*
 * Take a buffer from one of the class's own pools rather than from the shared
 * udc_ep_pool.
 */
static struct net_buf *iio_usb_buf_alloc(struct net_buf_pool *pool, const uint8_t ep)
{
	struct net_buf *buf;
	struct iio_usb_buf_info *bi;

	buf = net_buf_alloc(pool, K_NO_WAIT);
	if (buf == NULL) {
		return NULL;
	}

	bi = (struct iio_usb_buf_info *)udc_get_buf_info(buf);
	memset(bi, 0, sizeof(*bi));
	bi->udc.ep = ep;

	return buf;
}

static uint32_t iio_usb_buf_session(const struct net_buf *const buf)
{
	return ((struct iio_usb_buf_info *)udc_get_buf_info(buf))->session;
}

/*
 * Arm a receive buffer, if the pipe's credit allows. A bulk OUT is never
 * dequeued: on MAX32 that wedges the endpoint without aborting the read in
 * progress, so an armed buffer stays armed until it completes or the stack
 * tears the configuration down.
 */
static int iio_usb_arm_rx(struct usbd_class_data *const c_data, struct iio_usb_pipe *pipe)
{
	struct iio_usb_data *data = usbd_class_get_private(c_data);
	struct net_buf *buf;
	int err;

	if (!data->enabled) {
		return -ENODEV;
	}

	if (k_sem_take(&pipe->rx_credit, K_NO_WAIT)) {
		return 0;
	}

	buf = iio_usb_buf_alloc(data->rx_pool, iio_usb_get_bulk_out(c_data, pipe->idx));
	if (buf == NULL) {
		/* The pool holds every pipe's full depth, so the accounting is wrong */
		LOG_ERR("Pipe %u: RX pool empty while holding credit", pipe->idx);
		k_sem_give(&pipe->rx_credit);
		return -ENOMEM;
	}

	/* MAX32 completes a bulk OUT after one packet whatever the buffer size */
	buf->size = MIN(iio_usb_get_bulk_mps(c_data, pipe->idx * 2 + 1), buf->size);

	err = usbd_ep_enqueue(c_data, buf);
	if (err) {
		LOG_ERR("Pipe %u: Failed to enqueue RX buffer: %d", pipe->idx, err);
		net_buf_unref(buf);
		k_sem_give(&pipe->rx_credit);
		return err;
	}

	LOG_DBG("Pipe %u: RX buffer armed", pipe->idx);
	return 0;
}

/*
 * Release a receive buffer and arm a replacement. The credit goes back first,
 * since the completion handler may be arming the same pipe concurrently.
 */
static void iio_usb_release_rx(struct usbd_class_data *const c_data, struct iio_usb_pipe *pipe,
			       struct net_buf *buf)
{
	net_buf_unref(buf);
	k_sem_give(&pipe->rx_credit);
	iio_usb_arm_rx(c_data, pipe);
}

/*
 * Start a new session on a pipe. Bumping the number is the whole of it: buffers
 * queued or armed for the previous one carry an older stamp and are dropped as
 * they are popped.
 */
static void iio_usb_pipe_new_session(struct iio_usb_pipe *pipe)
{
	pipe->rx_err = 0;
	atomic_set(&pipe->rx_err_session, 0);
	atomic_inc(&pipe->session);

	/* Wake a reader so it discards anything stale promptly */
	k_sem_give(&pipe->rx_sem);
}

/* Report a receive error against the current session and wake its reader */
static void iio_usb_pipe_fail_rx(struct iio_usb_pipe *pipe, const int err)
{
	pipe->rx_err = err;
	atomic_set(&pipe->rx_err_session, atomic_get(&pipe->session));
	k_sem_give(&pipe->rx_sem);
}

static const void *iio_usb_get_desc(struct usbd_class_data *const c_data, const enum usbd_speed speed)
{
	struct iio_usb_data *data = usbd_class_get_private(c_data);

	if (USBD_SUPPORTS_HIGH_SPEED && speed == USBD_SPEED_HS) {
		return data->hs_desc;
	}

	return data->fs_desc;
}

static int iio_usb_request_handler(struct usbd_class_data *const c_data, struct net_buf *const buf,
				   const int err)
{
	struct udc_buf_info *bi = udc_get_buf_info(buf);
	struct iio_usb_data *data = usbd_class_get_private(c_data);
	const uint8_t ep = bi->ep;
	int pipe_idx;
	struct iio_usb_pipe *pipe;

	pipe_idx = find_pipe_by_ep(c_data, ep);
	if (pipe_idx < 0) {
		LOG_ERR("Unknown endpoint 0x%02x", ep);
		net_buf_unref(buf);
		return -EINVAL;
	}
	pipe = &data->pipes[pipe_idx];

	if (ep == iio_usb_get_bulk_out(c_data, pipe_idx)) {
		LOG_DBG("Pipe %d RX complete: err=%d, len=%u", pipe_idx, err, buf->len);

		if (err == 0 && buf->len > 0 && atomic_get(&pipe->open)) {
			/*
			 * Stamp the session the data arrived for, not the one
			 * it was armed for: a buffer outlives its arming
			 * session.
			 */
			((struct iio_usb_buf_info *)bi)->session =
				(uint32_t)atomic_get(&pipe->session);
			k_fifo_put(&pipe->rx_fifo, buf);
			k_sem_give(&pipe->rx_sem);
			iio_usb_arm_rx(c_data, pipe);
			return 0;
		}

		if (err) {
			/* Not portable: a dequeue gives -ECONNABORTED, MAX32 a raw -1 */
			iio_usb_pipe_fail_rx(pipe, err);
		}

		/*
		 * Otherwise a zero-length packet, or one for a session the pipe
		 * has left. Dropping it must not touch rx_err: overwriting the
		 * shutdown that ended a session would leave its reader stuck.
		 */
		iio_usb_release_rx(c_data, pipe, buf);

	} else if (ep == iio_usb_get_bulk_in(c_data, pipe_idx)) {
		/* Sent data to host (TX) */
		LOG_DBG("Pipe %d TX complete: err=%d", pipe_idx, err);
		pipe->tx_err = err;
		net_buf_unref(buf);
		k_sem_give(&pipe->tx_sem);
	} else {
		/* Unreachable: find_pipe_by_ep() matched one of the two */
		net_buf_unref(buf);
	}

	return 0;
}

static int iio_usb_init(struct usbd_class_data *const c_data)
{
	struct usbd_context *usbd_ctx = usbd_class_get_ctx(c_data);
	struct iio_usb_data *data = usbd_class_get_private(c_data);
	struct iio_usb_desc *desc = data->desc;

	/* Store class data pointer for use in read/write functions */
	data->c_data = c_data;

	/*
	 * Endpoint addresses are deliberately not read here: this runs once per
	 * registered speed, before the other speed's array is assigned.
	 */
	for (int i = 0; i < data->num_pipes; i++) {
		struct iio_usb_pipe *pipe = &data->pipes[i];

		pipe->idx = i;
		pipe->data = data;
		k_fifo_init(&pipe->rx_fifo);
		k_sem_init(&pipe->rx_sem, 0, K_SEM_MAX_LIMIT);
		k_sem_init(&pipe->rx_credit, data->rx_depth, data->rx_depth);
		k_sem_init(&pipe->tx_sem, 0, 1);
		k_sem_init(&pipe->start_sem, 0, 1);
		pipe->rx_err = 0;
		atomic_set(&pipe->rx_err_session, 0);
		/* Session numbers start at one, so zero can mean "no error published" */
		atomic_set(&pipe->session, 1);
		pipe->rx_cur = NULL;
		pipe->rx_cur_off = 0;
		pipe->tx_err = 0;
		atomic_set(&pipe->open, 0);
	}

	if (desc->if0.iInterface == 0) {
		if (usbd_add_descriptor(usbd_ctx, data->iface_str_desc)) {
			LOG_ERR("Failed to add IIO interface string descriptor");
		} else {
			desc->if0.iInterface = usbd_str_desc_get_idx(data->iface_str_desc);
			LOG_INF("IIO interface string at index %u", desc->if0.iInterface);
		}
	}

	/* The pipes are usable now, so release the server thread. This runs once
	 * per registered speed, and the semaphore caps at one.
	 */
	k_sem_give(&data->init_sem);

	return 0;
}

/*
 * Reads exactly size bytes: iiod frames are length-prefixed and the host sizes
 * each read to match, so a short read would desynchronise the stream. This is
 * also the only place a buffer stamped for a session the pipe has left is
 * dropped.
 */
static ssize_t iiod_usb_pipe_read(struct iiod_pdata *pdata, void *buf, size_t size)
{
	struct iio_usb_pipe *pipe = (struct iio_usb_pipe *)pdata;
	struct usbd_class_data *c_data = pipe->data->c_data;
	uint8_t *dest = buf;
	size_t bytes_read = 0;

	LOG_DBG("Pipe %u: read %zu bytes requested", pipe->idx, size);

	while (bytes_read < size) {
		struct net_buf *done;
		size_t avail;

		if ((uint32_t)atomic_get(&pipe->session) != pipe->serving) {
			/* Another session has started; its bytes are not ours */
			LOG_DBG("Pipe %u: session %u superseded", pipe->idx, pipe->serving);
			return -ESHUTDOWN;
		}

		if (pipe->rx_cur == NULL) {
			pipe->rx_cur = k_fifo_get(&pipe->rx_fifo, K_NO_WAIT);
			pipe->rx_cur_off = 0;
		}

		if (pipe->rx_cur == NULL) {
			if (pipe->rx_err &&
			    (uint32_t)atomic_get(&pipe->rx_err_session) == pipe->serving) {
				/* Teardown cancels the armed transfer, so that
				 * completion arriving is expected.
				 */
				if (atomic_get(&pipe->open)) {
					LOG_ERR("Pipe %u: USB RX error: %d", pipe->idx,
						pipe->rx_err);
				} else {
					LOG_DBG("Pipe %u: RX error during shutdown: %d",
						pipe->idx, pipe->rx_err);
				}
				return pipe->rx_err;
			}

			k_sem_take(&pipe->rx_sem, K_FOREVER);
			continue;
		}

		if (iio_usb_buf_session(pipe->rx_cur) != pipe->serving) {
			done = pipe->rx_cur;
			pipe->rx_cur = NULL;
			iio_usb_release_rx(c_data, pipe, done);
			continue;
		}

		avail = MIN(pipe->rx_cur->len - pipe->rx_cur_off, size - bytes_read);
		memcpy(dest + bytes_read, pipe->rx_cur->data + pipe->rx_cur_off, avail);
		bytes_read += avail;
		pipe->rx_cur_off += avail;

		if (pipe->rx_cur_off >= pipe->rx_cur->len) {
			done = pipe->rx_cur;
			pipe->rx_cur = NULL;
			iio_usb_release_rx(c_data, pipe, done);
		}
	}

	return bytes_read;
}

static ssize_t iiod_usb_pipe_write(struct iiod_pdata *pdata, const void *buf, size_t size)
{
	struct iio_usb_pipe *pipe = (struct iio_usb_pipe *)pdata;
	struct iio_usb_data *data = pipe->data;
	struct usbd_class_data *c_data = data->c_data;
	const uint8_t *src = (const uint8_t *)buf;
	struct net_buf *net_buf;
	int err;

	if (c_data == NULL || !data->enabled) {
		return -ENODEV;
	}

	LOG_DBG("Pipe %u: TX %zu bytes", pipe->idx, size);

	size_t bytes_sent = 0;

	while (bytes_sent < size) {
		size_t chunk_size = MIN(size - bytes_sent, data->tx_buf_size);

		if ((uint32_t)atomic_get(&pipe->session) != pipe->serving) {
			/*
			 * The session ended part-way through this response. The
			 * chunk already enqueued cannot be taken back, so that
			 * one is still delivered; everything after it is not.
			 */
			LOG_DBG("Pipe %u: session %u superseded mid-write",
				pipe->idx, pipe->serving);
			return -ESHUTDOWN;
		}

		net_buf = iio_usb_buf_alloc(data->tx_pool,
					    iio_usb_get_bulk_in(c_data, pipe->idx));

		if (net_buf == NULL) {
			LOG_ERR("Pipe %u: Failed to allocate TX buffer for %zu bytes",
				pipe->idx, chunk_size);
			return bytes_sent > 0 ? bytes_sent : -ENOMEM;
		}

		net_buf_add_mem(net_buf, src + bytes_sent, chunk_size);
		pipe->tx_err = 0;

		err = usbd_ep_enqueue(c_data, net_buf);
		if (err) {
			LOG_ERR("Pipe %u: Failed to enqueue TX buffer: %d", pipe->idx, err);
			net_buf_unref(net_buf);
			return bytes_sent > 0 ? bytes_sent : err;
		}

		/*
		 * A transmit cannot be dequeued safely, so it must be waited
		 * out; it always completes via a host read or the stack's
		 * teardown. This is the pipe's own thread, so until the wait
		 * ends the pipe serves no new session.
		 */
		if (k_sem_take(&pipe->tx_sem, K_MSEC(CONFIG_LIBIIO_IIOD_USB_TX_WARN_MS))) {
			LOG_WRN("Pipe %u: transmit outstanding after %u ms, host not reading",
				pipe->idx, CONFIG_LIBIIO_IIOD_USB_TX_WARN_MS);
			k_sem_take(&pipe->tx_sem, K_FOREVER);
		}

		if (pipe->tx_err) {
			LOG_ERR("Pipe %u: USB TX transfer failed: %d", pipe->idx,
				pipe->tx_err);
			return bytes_sent > 0 ? bytes_sent : pipe->tx_err;
		}

		bytes_sent += chunk_size;
	}

	return bytes_sent;
}

/*
 * Serve one session on a pipe, then park until the host opens it again.
 *
 * Every pipe's thread, including the command channel's, is created once at
 * startup and never joined: the control handler runs on the USB device stack's
 * thread and must not wait on a pipe thread, which may itself be blocked on a
 * completion only that same stack thread can deliver.
 */
static void iio_usb_pipe_thread(void *p1, void *p2, void *p3)
{
	struct iio_usb_pipe *pipe = p1;
	struct iio_usb_data *data = pipe->data;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		k_sem_take(&pipe->start_sem, K_FOREVER);

		if (!atomic_get(&pipe->open)) {
			/* Closed again before this thread reached it. */
			continue;
		}

		/*
		 * Adopt the session the control handler set, and release the
		 * buffer the previous one was part-way through to reclaim its
		 * credit. The rest drop as they are popped.
		 */
		pipe->serving = (uint32_t)atomic_get(&pipe->session);

		if (pipe->rx_cur != NULL) {
			struct net_buf *stale = pipe->rx_cur;

			pipe->rx_cur = NULL;
			iio_usb_release_rx(data->c_data, pipe, stale);
		}

		LOG_INF("Pipe %u: serving session %u", pipe->idx, pipe->serving);

		iiod_interpreter(data->ctx, (struct iiod_pdata *)pipe, iiod_usb_pipe_read,
				 iiod_usb_pipe_write, data->xml, data->xml_len);

		/*
		 * pipe->open is not cleared here: it belongs to the control
		 * handler, and clearing it would race a reopen.
		 */
		LOG_INF("Pipe %u: session %u done", pipe->idx, pipe->serving);
	}
}

/*
 * Handle IIO vendor control requests for pipe management.
 * The host-side libiio sends these as VENDOR|RECIPIENT_INTERFACE requests:
 *   bRequest = command (OPEN/CLOSE/RESET)
 *   wValue   = pipe_id
 */
static int iio_usb_control_to_dev(struct usbd_class_data *c_data,
				  const struct usb_setup_packet *const setup,
				  const struct net_buf *const buf)
{
	struct iio_usb_data *data = usbd_class_get_private(c_data);
	uint8_t request = setup->bRequest;
	uint16_t pipe_id = setup->wValue;
	int err;

	/* The stack routes every non-standard request for this interface here */
	if (setup->RequestType.type != USB_REQTYPE_TYPE_VENDOR) {
		return -ENOTSUP;
	}

	LOG_INF("Control to dev: bRequest=0x%02x, wValue=0x%04x, wIndex=0x%04x", request, pipe_id,
		setup->wIndex);

	switch (request) {
	case IIO_USD_CMD_RESET_PIPES:
		LOG_INF("RESET_PIPES");
		/*
		 * Pipe 0 never gets CLOSE_PIPE, so this is its only teardown
		 * signal. A fresh client expects its first eight bytes read as
		 * a command header.
		 */
		for (int i = 0; i < data->num_pipes; i++) {
			if (atomic_set(&data->pipes[i].open, 0)) {
				iio_usb_pipe_fail_rx(&data->pipes[i], -ESHUTDOWN);
			}
		}
		break;

	case IIO_USD_CMD_OPEN_PIPE:
		LOG_INF("OPEN_PIPE %u", pipe_id);
		if (pipe_id >= data->num_pipes) {
			LOG_ERR("Invalid pipe_id %u (max %d)", pipe_id, data->num_pipes - 1);
			return -EINVAL;
		}
		/*
		 * Always start fresh, even if already open: a session can end
		 * silently (receive error, interface reset), and the host sends
		 * a redundant OPEN_PIPE 0 on context creation.
		 */
		if (data->ctx == NULL) {
			/* No context means no pipe can ever answer, so refuse */
			LOG_ERR("Pipe %u: no IIO context", pipe_id);
			return -EIO;
		}

		/*
		 * Number the new session first. A previous interpreter still
		 * unwinding leaves on its next read once it sees this.
		 */
		iio_usb_pipe_new_session(&data->pipes[pipe_id]);

		/*
		 * Usually a no-op, since buffers are not released on close. A
		 * failed arm must be refused: an unarmed bulk OUT NAKs every
		 * packet, stalling the host for its whole connect timeout.
		 */
		err = iio_usb_arm_rx(c_data, &data->pipes[pipe_id]);
		if (err) {
			LOG_ERR("Pipe %u: cannot arm receive endpoint: %d", pipe_id, err);
			return err;
		}

		atomic_set(&data->pipes[pipe_id].open, 1);
		k_sem_give(&data->pipes[pipe_id].start_sem);
		break;

	case IIO_USD_CMD_CLOSE_PIPE:
		LOG_INF("CLOSE_PIPE %u", pipe_id);
		if (pipe_id >= data->num_pipes) {
			return -EINVAL;
		}
		/*
		 * The buffer stays armed - dequeuing a bulk OUT wedges the
		 * controller - and keeps its credit, so a reopen cannot
		 * double-arm.
		 */
		if (atomic_set(&data->pipes[pipe_id].open, 0)) {
			iio_usb_pipe_fail_rx(&data->pipes[pipe_id], -ESHUTDOWN);
		}
		break;

	default:
		LOG_DBG("Unknown vendor request 0x%02x", request);
		return -ENOTSUP;
	}

	return 0;
}

/*
 * Check and log the endpoint map the stack settled on. This is the first point
 * it can be checked, since the bus speed decides the live descriptor array.
 * Neither failure is reachable on a class the stack has registered, so this
 * guards the assumption the rest of the file makes.
 */
static int iio_usb_validate_eps(struct usbd_class_data *const c_data)
{
	struct iio_usb_data *data = usbd_class_get_private(c_data);
	int ret = 0;

	for (int i = 0; i < data->num_pipes; i++) {
		uint8_t ep_in = iio_usb_get_bulk_in(c_data, i);
		uint8_t ep_out = iio_usb_get_bulk_out(c_data, i);

		LOG_INF("Pipe %d: IN=0x%02x OUT=0x%02x", i, ep_in, ep_out);

		if (!USB_EP_DIR_IS_IN(ep_in) || USB_EP_GET_IDX(ep_in) == 0 ||
		    !USB_EP_DIR_IS_OUT(ep_out) || USB_EP_GET_IDX(ep_out) == 0) {
			LOG_ERR("Pipe %d: endpoint pair was not assigned (IN=0x%02x OUT=0x%02x)",
				i, ep_in, ep_out);
			ret = -ENODEV;
			continue;
		}

		for (int j = 0; j < i; j++) {
			if (iio_usb_get_bulk_in(c_data, j) == ep_in ||
			    iio_usb_get_bulk_out(c_data, j) == ep_out) {
				LOG_ERR("Pipe %d shares an endpoint with pipe %d", i, j);
				ret = -ENODEV;
			}
		}
	}

	return ret;
}

/*
 * Arm every pipe's receive endpoint, as far as its credit allows. Buffers
 * claimed here live for the whole configuration; credit is only returned by
 * the completion that releases a buffer, so anything still outstanding arms
 * itself as it comes back.
 */
static void iio_usb_arm_all(struct usbd_class_data *const c_data)
{
	struct iio_usb_data *data = usbd_class_get_private(c_data);

	for (int i = 0; i < data->num_pipes; i++) {
		struct iio_usb_pipe *pipe = &data->pipes[i];

		for (int n = 0; n < data->rx_depth; n++) {
			if (iio_usb_arm_rx(c_data, pipe)) {
				LOG_ERR("Pipe %d: failed to arm receive endpoint", i);
				break;
			}
		}
	}
}

static void iio_usb_enable(struct usbd_class_data *const c_data)
{
	struct iio_usb_data *data = usbd_class_get_private(c_data);

	LOG_INF("IIO USB class enabled");

	if (iio_usb_validate_eps(c_data)) {
		/*
		 * Leave the class disabled rather than arm a map that cannot
		 * work. Every OPEN_PIPE then stalls, which the host reports,
		 * instead of the pipes silently never answering.
		 */
		LOG_ERR("IIO USB class left disabled: unusable endpoint map");
		return;
	}

	data->enabled = true;

	iio_usb_arm_all(c_data);
}

/*
 * SET_INTERFACE disables, dequeues and re-enables every endpoint on the
 * interface, and this is the only notice the class gets - there is no
 * disable/enable pair around it, even when the alternate is the one already
 * selected. Arm the pipes again rather than rely on the cancelled buffers'
 * completions arriving after the request has returned.
 */
static void iio_usb_update(struct usbd_class_data *const c_data, const uint8_t iface,
			   const uint8_t alternate)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(alternate);

	LOG_INF("IIO USB interface reset");
	iio_usb_arm_all(c_data);
}

static void iio_usb_disable(struct usbd_class_data *const c_data)
{
	struct iio_usb_data *data = usbd_class_get_private(c_data);

	LOG_INF("IIO USB class disabled");
	data->enabled = false;

	/* Signal all open pipes to exit their interpreter loops */
	for (int i = 0; i < data->num_pipes; i++) {
		/* Writers are released by their own queued completions, so
		 * giving tx_sem here would double-count.
		 */
		if (atomic_set(&data->pipes[i].open, 0)) {
			iio_usb_pipe_fail_rx(&data->pipes[i], -ESHUTDOWN);
		}
	}
}

static const struct usbd_class_api iio_usb_api = {
	.get_desc = iio_usb_get_desc,
	.control_to_dev = iio_usb_control_to_dev,
	.request = iio_usb_request_handler,
	.update = iio_usb_update,
	.enable = iio_usb_enable,
	.disable = iio_usb_disable,
	.init = iio_usb_init,
};

/*
 * Bring up the resources every pipe shares, start a thread for each data pipe,
 * then serve the command channel in this thread. The context is created once
 * and kept forever, so there is nothing to refresh on reconnect.
 */
static void iiod_usb_thread_fn(struct iio_usb_data *data)
{
	struct iio_context_params ctx_params = {0};

	/* Nothing below may run before .init has set the pipes up. */
	k_sem_take(&data->init_sem, K_FOREVER);

	LOG_INF("Initializing tinyiiod resources...");
	if (iiod_init() < 0) {
		LOG_ERR("Failed to initialize tinyiiod resources");
		return;
	}

	LOG_INF("Creating shared IIO context...");
	data->ctx = iio_create_context(&ctx_params, "zephyr:");
	if (iio_err(data->ctx)) {
		LOG_ERR("Context creation failed");
		data->ctx = NULL;
		iiod_cleanup();
		return;
	}

	LOG_INF("Getting xml data");
	data->xml = iio_context_get_xml(data->ctx);
	if (iio_err(data->xml)) {
		LOG_ERR("Error getting context XML");
		iio_context_destroy(data->ctx);
		data->ctx = NULL;
		data->xml = NULL;
		iiod_cleanup();
		return;
	}

	data->xml_len = strlen(data->xml) + 1;
	LOG_INF("XML ready, length: %zu bytes", data->xml_len);

	for (int i = 1; i < data->num_pipes; i++) {
		int idx = i - 1;

		/*
		 * K_THREAD_STACK_LEN, not the configured size: an array member
		 * is padded out to the architecture's stack alignment, so
		 * striding by the configured size would overlap the stacks.
		 */
		k_thread_create(&data->pipe_threads[idx],
				data->pipe_stacks +
					idx * K_THREAD_STACK_LEN(
						CONFIG_LIBIIO_IIOD_USB_PIPE_THREAD_STACK_SIZE),
				CONFIG_LIBIIO_IIOD_USB_PIPE_THREAD_STACK_SIZE,
				iio_usb_pipe_thread, &data->pipes[i], NULL, NULL,
				CONFIG_LIBIIO_IIOD_USB_THREAD_PRIORITY, 0, K_NO_WAIT);
#ifdef CONFIG_THREAD_NAME
		{
			char name[CONFIG_THREAD_MAX_NAME_LEN];

			snprintf(name, sizeof(name), "iiod_usb_pipe%d", i);
			k_thread_name_set(&data->pipe_threads[idx], name);
		}
#endif
	}

	/* This thread serves pipe 0, and never returns. */
	iio_usb_pipe_thread(&data->pipes[0], NULL, NULL);
}

/*
 * Per-instance descriptor pointer macros.
 * These reference a specific instance's descriptor struct.
 */
#define FS_EP_PTR(n, inst)                                                                         \
	(struct usb_desc_header *)&iio_usb_desc_##inst.if0_ep[n * 2],                              \
		(struct usb_desc_header *)&iio_usb_desc_##inst.if0_ep[n * 2 + 1]

#define HS_EP_PTR(n, inst)                                                                         \
	(struct usb_desc_header *)&iio_usb_desc_##inst.if0_hs_ep[n * 2],                           \
		(struct usb_desc_header *)&iio_usb_desc_##inst.if0_hs_ep[n * 2 + 1]

#define DECLARE_FS_EP_PTR(n, inst) FS_EP_PTR(n, inst)
#define DECLARE_HS_EP_PTR(n, inst) HS_EP_PTR(n, inst)

/*
 * Per-instance device definition macro.
 * Instantiates descriptors, data structures, pipe threads, and the
 * IIOD interpreter thread for each DTS node with compatible "adi,iio-usb".
 */
#define IIO_USB_DEVICE_DEFINE(inst)                                                                \
                                                                                                   \
BUILD_ASSERT(DT_INST_ON_BUS(inst, usb),                                                            \
	"node " DT_NODE_PATH(DT_DRV_INST(inst))                                                    \
	" is not assigned to a USB device controller");                                            \
                                                                                                   \
USBD_DESC_STRING_DEFINE(iio_iface_str_desc_##inst, "IIO", USBD_DUT_STRING_INTERFACE);              \
                                                                                                   \
static struct iio_usb_desc iio_usb_desc_##inst = {                                                 \
	.if0 = {                                                                                   \
		.bLength = sizeof(struct usb_if_descriptor),                                       \
		.bDescriptorType = USB_DESC_INTERFACE,                                             \
		.bInterfaceNumber = 0,                                                             \
		.bAlternateSetting = 0,                                                            \
		.bNumEndpoints = DT_INST_PROP(inst, num_pipes) * 2,                                \
		.bInterfaceClass = USB_BCC_VENDOR,                                                 \
		.bInterfaceSubClass = 0,                                                           \
		.bInterfaceProtocol = 0,                                                           \
		.iInterface = 0,                                                                   \
	},                                                                                         \
	.if0_ep = {                                                                                \
		LISTIFY(DT_INST_PROP(inst, num_pipes), DECLARE_EP_FS_PAIR, (,))                    \
	},                                                                                         \
	.if0_hs_ep = {                                                                             \
		LISTIFY(DT_INST_PROP(inst, num_pipes), DECLARE_EP_HS_PAIR, (,))                    \
	},                                                                                         \
	.nil_desc = {                                                                              \
		.bLength = 0,                                                                      \
		.bDescriptorType = 0,                                                              \
	},                                                                                         \
};                                                                                                 \
                                                                                                   \
static const struct usb_desc_header *const iio_fs_desc_##inst[] = {                                \
	(struct usb_desc_header *)&iio_usb_desc_##inst.if0,                                        \
	LISTIFY(DT_INST_PROP(inst, num_pipes), DECLARE_FS_EP_PTR, (,), inst),                      \
	(struct usb_desc_header *)&iio_usb_desc_##inst.nil_desc,                                   \
};                                                                                                 \
                                                                                                   \
static const struct usb_desc_header *const iio_hs_desc_##inst[] = {                                \
	(struct usb_desc_header *)&iio_usb_desc_##inst.if0,                                        \
	LISTIFY(DT_INST_PROP(inst, num_pipes), DECLARE_HS_EP_PTR, (,), inst),                      \
	(struct usb_desc_header *)&iio_usb_desc_##inst.nil_desc,                                   \
};                                                                                                 \
                                                                                                   \
BUILD_ASSERT(DT_INST_PROP(inst, num_pipes) <= IIO_USB_MAX_PIPES,                                   \
	"node " DT_NODE_PATH(DT_DRV_INST(inst))                                                    \
	" num-pipes exceeds IIO_USB_MAX_PIPES");                                                   \
                                                                                                   \
/* Against the largest packet the build can use, not the speed the bus settles  */                 \
/* on: a chunk has to end on a packet boundary at every speed it can run at.    */                 \
BUILD_ASSERT(DT_INST_PROP(inst, tx_buf_size) % USBD_MAX_BULK_MPS == 0,                             \
	"node " DT_NODE_PATH(DT_DRV_INST(inst))                                                    \
	" tx-buf-size is not a multiple of the bulk endpoint max packet size");                    \
                                                                                                   \
UDC_BUF_POOL_DEFINE(iio_usb_rx_pool_##inst,                                                        \
		    DT_INST_PROP(inst, num_pipes) * DT_INST_PROP(inst, rx_depth),                  \
		    USBD_MAX_BULK_MPS, sizeof(struct iio_usb_buf_info), NULL);                     \
                                                                                                   \
UDC_BUF_POOL_DEFINE(iio_usb_tx_pool_##inst, DT_INST_PROP(inst, num_pipes),                         \
		    DT_INST_PROP(inst, tx_buf_size), sizeof(struct iio_usb_buf_info), NULL);       \
                                                                                                   \
COND_CODE_1(UTIL_BOOL(DT_INST_PROP(inst, num_pipes) - 1), (                                        \
	static K_THREAD_STACK_ARRAY_DEFINE(iio_usb_pipe_stacks_##inst,                             \
					   DT_INST_PROP(inst, num_pipes) - 1,                      \
					   CONFIG_LIBIIO_IIOD_USB_PIPE_THREAD_STACK_SIZE);         \
	static struct k_thread iio_usb_pipe_threads_##inst                                         \
		[DT_INST_PROP(inst, num_pipes) - 1];                                               \
), ())                                                                                             \
                                                                                                   \
static struct iio_usb_data iio_usb_data_##inst = {                                                 \
	.desc = &iio_usb_desc_##inst,                                                              \
	.fs_desc = iio_fs_desc_##inst,                                                             \
	.hs_desc = iio_hs_desc_##inst,                                                             \
	.iface_str_desc = &iio_iface_str_desc_##inst,                                              \
	.init_sem = Z_SEM_INITIALIZER(iio_usb_data_##inst.init_sem, 0, 1),                         \
	.num_pipes = DT_INST_PROP(inst, num_pipes),                                                \
	.tx_buf_size = DT_INST_PROP(inst, tx_buf_size),                                            \
	.rx_depth = DT_INST_PROP(inst, rx_depth),                                                  \
	.rx_pool = &iio_usb_rx_pool_##inst,                                                        \
	.tx_pool = &iio_usb_tx_pool_##inst,                                                        \
	.c_data = NULL,                                                                            \
	.enabled = false,                                                                          \
	COND_CODE_1(UTIL_BOOL(DT_INST_PROP(inst, num_pipes) - 1), (                                \
		.pipe_threads = iio_usb_pipe_threads_##inst,                                       \
		.pipe_stacks = (k_thread_stack_t *)iio_usb_pipe_stacks_##inst,                     \
	), (                                                                                       \
		.pipe_threads = NULL,                                                              \
		.pipe_stacks = NULL,                                                               \
	))                                                                                         \
};                                                                                                 \
                                                                                                   \
USBD_DEFINE_CLASS(iio_usb_##inst, &iio_usb_api, &iio_usb_data_##inst, NULL);                       \
                                                                                                   \
static void iiod_usb_thread_##inst(void *p1, void *p2, void *p3)                                   \
{                                                                                                  \
	ARG_UNUSED(p1);                                                                            \
	ARG_UNUSED(p2);                                                                            \
	ARG_UNUSED(p3);                                                                            \
	iiod_usb_thread_fn(&iio_usb_data_##inst);                                                  \
}                                                                                                  \
                                                                                                   \
K_THREAD_DEFINE(iiod_usb_##inst, CONFIG_LIBIIO_IIOD_USB_THREAD_STACK_SIZE,                         \
		iiod_usb_thread_##inst, NULL, NULL, NULL,                                          \
		CONFIG_LIBIIO_IIOD_USB_THREAD_PRIORITY, 0, 1);

/* Instantiate for each DTS node with compatible "adi,iio-usb" */
DT_INST_FOREACH_STATUS_OKAY(IIO_USB_DEVICE_DEFINE)
