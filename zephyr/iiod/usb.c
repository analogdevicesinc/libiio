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

/*
 * UDC buffer metadata, extended with the session the buffer belongs to.
 *
 * The udc_buf_info has to come first so the buffer can still be handed to the
 * stack unchanged; usbd_uvc.c extends it the same way.
 */
struct iio_usb_buf_info {
	struct udc_buf_info udc;
	uint32_t epoch;
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
 * Per-pipe state.
 *
 * Received packets reach the interpreter as whole net_bufs, each carrying the
 * session it arrived for, so staleness is decided per buffer where it is popped.
 */
struct iio_usb_pipe {
	/* Assigned once; the endpoint addresses themselves are resolved per use. */
	uint8_t idx;
	/* Receive buffers handed over by the completion handler, oldest first. */
	struct k_fifo rx_fifo;
	/* Signalled whenever rx_fifo or rx_err changed. Never reset: the reader
	 * re-checks both, so a spurious count costs it one extra turn of its loop.
	 */
	struct k_sem rx_sem;
	/* Credit for receive buffers, taken before arming one and given back when
	 * it is released, so a pipe cannot hold more than rx-depth of them.
	 */
	struct k_sem rx_credit;
	struct k_sem tx_sem;
	/* The session this pipe is serving. Bumped at each session start, leaving
	 * anything already queued for the previous one with an older stamp.
	 */
	atomic_t epoch;
	/* rx_err applies only to the session named by rx_err_epoch, so a shutdown
	 * cannot outlive the session it ended. Written by the device stack's
	 * thread, read by the reader.
	 */
	int rx_err;
	atomic_t rx_err_epoch;
	/* The buffer the reader is part-way through, and how far in. Touched only
	 * by this pipe's interpreter thread.
	 */
	struct net_buf *rx_cur;
	uint16_t rx_cur_off;
	int tx_err;
	bool open; /* Whether this pipe has been opened by host */
	struct iio_usb_data *data; /* back-pointer to parent */
};

struct iio_usb_data {
	struct iio_usb_desc *const desc;
	const struct usb_desc_header *const *const fs_desc;
	const struct usb_desc_header *const *const hs_desc;
	struct usbd_desc_node *const iface_str_desc;
	struct usbd_class_data *c_data;
	/* Statically initialised in the instance definition: the server thread
	 * waits on this from boot, which is long before .init runs when the
	 * application supplies its own USB device and calls usbd_init() itself.
	 */
	struct k_sem enabled_sem;
	uint8_t num_pipes;
	uint16_t rx_buf_size;
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
 *
 * These cannot be cached: the first .init sees the not-yet-assigned speed's
 * array still holding placeholder addresses. The two speed arrays can also
 * legitimately disagree, since each is assigned against its own endpoint
 * bitmap.
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

/*
 * The live max packet size. The stack writes the value it settled on back into
 * the descriptor, so take it from there rather than assuming 64 or 512.
 */
static uint16_t iio_usb_get_bulk_mps(struct usbd_class_data *const c_data)
{
	return sys_le16_to_cpu(iio_usb_ep_desc(c_data, 0)->wMaxPacketSize);
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

static uint32_t iio_usb_buf_epoch(const struct net_buf *const buf)
{
	return ((struct iio_usb_buf_info *)udc_get_buf_info(buf))->epoch;
}

/*
 * The credit bounds what the two racing callers - the completion handler and
 * a reader that just released a buffer - can arm between them to rx-depth.
 *
 * Nothing ever dequeues a bulk OUT: on MAX32 that wedges the endpoint without
 * aborting the read already in progress, so an armed buffer stays armed until
 * it completes or the stack tears the configuration down.
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
		/*
		 * The pool holds every pipe's full depth, so holding credit and
		 * finding it empty means the accounting is wrong.
		 */
		LOG_ERR("Pipe %u: RX pool empty while holding credit", pipe->idx);
		k_sem_give(&pipe->rx_credit);
		return -ENOMEM;
	}

	/*
	 * Cap the transfer at the live max packet size. MAX32 completes a bulk
	 * OUT after a single packet whatever the buffer size, so a larger one
	 * only makes the completion granularity differ between controllers.
	 */
	buf->size = MIN(iio_usb_get_bulk_mps(c_data), buf->size);

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
 * Start a new session on a pipe, from the device stack's thread.
 *
 * Bumping the epoch is the whole of it: buffers already queued or armed for the
 * previous session carry an older stamp and are dropped as they are popped, and
 * an error published for it no longer applies.
 */
static void iio_usb_pipe_new_session(struct iio_usb_pipe *pipe)
{
	pipe->rx_err = 0;
	atomic_set(&pipe->rx_err_epoch, 0);
	atomic_inc(&pipe->epoch);

	/* Wake a reader so it discards anything stale promptly rather than on
	 * its next packet.
	 */
	k_sem_give(&pipe->rx_sem);
}

/*
 * Report a receive error against the current session and wake its reader.
 * Stamping it stops a shutdown outliving the session it ended.
 */
static void iio_usb_pipe_fail_rx(struct iio_usb_pipe *pipe, const int err)
{
	pipe->rx_err = err;
	atomic_set(&pipe->rx_err_epoch, atomic_get(&pipe->epoch));
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
	struct udc_buf_info *bi = (struct udc_buf_info *)net_buf_user_data(buf);
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

		if (err == 0 && buf->len > 0 && pipe->open) {
			/*
			 * Stamp the session the data arrived for, not the one the
			 * buffer was armed for, since a buffer outlives its arming
			 * session. Arm a replacement now so the endpoint does not go
			 * bare while the reader copies out of this one.
			 */
			((struct iio_usb_buf_info *)bi)->epoch =
				(uint32_t)atomic_get(&pipe->epoch);
			k_fifo_put(&pipe->rx_fifo, buf);
			k_sem_give(&pipe->rx_sem);
			iio_usb_arm_rx(c_data, pipe);
			return 0;
		}

		if (err) {
			/*
			 * The value is not portable - a dequeue gives -ECONNABORTED,
			 * MAX32 reports hardware errors as a raw -1 - so report it
			 * rather than matching on it.
			 */
			iio_usb_pipe_fail_rx(pipe, err);
		}

		/*
		 * Otherwise a zero-length packet, or one for a session the pipe has
		 * left. Drop it without touching rx_err: overwriting the shutdown
		 * that ended a session would leave its reader waiting forever.
		 */
		iio_usb_release_rx(c_data, pipe, buf);

	} else if (ep == iio_usb_get_bulk_in(c_data, pipe_idx)) {
		/* Sent data to host (TX) */
		LOG_DBG("Pipe %d TX complete: err=%d", pipe_idx, err);
		pipe->tx_err = err;
		net_buf_unref(buf);
		k_sem_give(&pipe->tx_sem);
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
	 * registered speed, before the other speed's array is assigned, so
	 * anything cached now would be a placeholder. iio_usb_validate_eps()
	 * reports the resolved map from .enable instead, once the speed is known.
	 */
	for (int i = 0; i < data->num_pipes; i++) {
		struct iio_usb_pipe *pipe = &data->pipes[i];

		pipe->idx = i;
		pipe->data = data;
		k_fifo_init(&pipe->rx_fifo);
		k_sem_init(&pipe->rx_sem, 0, K_SEM_MAX_LIMIT);
		k_sem_init(&pipe->rx_credit, data->rx_depth, data->rx_depth);
		k_sem_init(&pipe->tx_sem, 0, 1);
		pipe->rx_err = 0;
		atomic_set(&pipe->rx_err_epoch, 0);
		/* Sessions are numbered from one so that zero can mean "no error
		 * has been published for any session".
		 */
		atomic_set(&pipe->epoch, 1);
		pipe->rx_cur = NULL;
		pipe->rx_cur_off = 0;
		pipe->tx_err = 0;
		pipe->open = false;
	}

	if (desc->if0.iInterface == 0) {
		if (usbd_add_descriptor(usbd_ctx, data->iface_str_desc)) {
			LOG_ERR("Failed to add IIO interface string descriptor");
		} else {
			desc->if0.iInterface = usbd_str_desc_get_idx(data->iface_str_desc);
			LOG_INF("IIO interface string at index %u", desc->if0.iInterface);
		}
	}

	return 0;
}

/*
 * Reads exactly size bytes: iiod frames are length-prefixed and the host sizes
 * each read to match, so a short read would desynchronise the stream. Buffers
 * stamped for a session this pipe has left are dropped here, the only place
 * staleness is handled.
 */
static ssize_t iiod_usb_pipe_read(struct iiod_pdata *pdata, void *buf, size_t size)
{
	struct iio_usb_pipe *pipe = (struct iio_usb_pipe *)pdata;
	struct usbd_class_data *c_data = pipe->data->c_data;
	uint8_t *dest = buf;
	size_t bytes_read = 0;

	LOG_DBG("Pipe %u: read %zu bytes requested", pipe->idx, size);

	while (bytes_read < size) {
		uint32_t epoch = (uint32_t)atomic_get(&pipe->epoch);
		struct net_buf *done;
		size_t avail;

		if (pipe->rx_cur == NULL) {
			pipe->rx_cur = k_fifo_get(&pipe->rx_fifo, K_NO_WAIT);
			pipe->rx_cur_off = 0;
		}

		if (pipe->rx_cur == NULL) {
			if (pipe->rx_err && (uint32_t)atomic_get(&pipe->rx_err_epoch) == epoch) {
				/*
				 * Only log as an error while the pipe is open.
				 * Teardown cancels the armed transfer, and that
				 * completion arriving is expected.
				 */
				if (pipe->open) {
					LOG_ERR("Pipe %u: USB RX error: %d", pipe->idx,
						pipe->rx_err);
				} else {
					LOG_DBG("Pipe %u: RX error during shutdown: %d",
						pipe->idx, pipe->rx_err);
				}
				return pipe->rx_err;
			}

			/* Nothing buffered and nothing wrong with this session. */
			k_sem_take(&pipe->rx_sem, K_FOREVER);
			continue;
		}

		if (iio_usb_buf_epoch(pipe->rx_cur) != epoch) {
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

		err = k_sem_take(&pipe->tx_sem, K_FOREVER);
		if (err) {
			LOG_ERR("Pipe %u: TX semaphore error: %d", pipe->idx, err);
			return bytes_sent > 0 ? bytes_sent : err;
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
 * Thread entry for data pipe interpreters (pipes 1..N-1).
 * Each runs its own iiod_interpreter using the pipe's endpoints.
 */
static void pipe_interpreter_thread(void *p1, void *p2, void *p3)
{
	struct iio_usb_pipe *pipe = (struct iio_usb_pipe *)p1;
	struct iio_usb_data *data = (struct iio_usb_data *)p2;
	int pipe_idx = (int)(intptr_t)p3;

	LOG_INF("Pipe %d: interpreter thread started", pipe_idx);

	iiod_interpreter(data->ctx, (struct iiod_pdata *)pipe, iiod_usb_pipe_read,
			 iiod_usb_pipe_write, data->xml, data->xml_len);

	LOG_INF("Pipe %d: interpreter thread exiting", pipe_idx);
	pipe->open = false;
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

	LOG_INF("Control to dev: bRequest=0x%02x, wValue=0x%04x, wIndex=0x%04x", request, pipe_id,
		setup->wIndex);

	switch (request) {
	case IIO_USD_CMD_RESET_PIPES:
		LOG_INF("RESET_PIPES");
		for (int i = 1; i < data->num_pipes; i++) {
			if (data->pipes[i].open) {
				data->pipes[i].open = false;
				iio_usb_pipe_fail_rx(&data->pipes[i], -ESHUTDOWN);
			}
		}
		/*
		 * Pipe 0 stays alive across RESET_PIPES, so give it a new session
		 * rather than an error: an abandoned connection can leave packets
		 * queued that the next client's first read would parse as a command
		 * header, and the bump makes the reader drop them.
		 */
		iio_usb_pipe_new_session(&data->pipes[0]);
		break;

	case IIO_USD_CMD_OPEN_PIPE:
		LOG_INF("OPEN_PIPE %u", pipe_id);
		if (pipe_id >= data->num_pipes) {
			LOG_ERR("Invalid pipe_id %u (max %d)", pipe_id, data->num_pipes - 1);
			return -EINVAL;
		}
		if (data->pipes[pipe_id].open) {
			LOG_WRN("Pipe %u already open", pipe_id);
			return 0;
		}
		/*
		 * Number the new session before arming, so the buffer armed for it
		 * is stamped with it and anything left from the previous one is not.
		 */
		iio_usb_pipe_new_session(&data->pipes[pipe_id]);

		/*
		 * Usually a no-op: buffers aren't released on close, so the pipe is
		 * already full. A failed arm must be refused rather than reported as
		 * open - an unarmed bulk OUT NAKs every packet, stalling the host on
		 * an unbounded connect timeout.
		 */
		err = iio_usb_arm_rx(c_data, &data->pipes[pipe_id]);
		if (err) {
			LOG_ERR("Pipe %u: cannot arm receive endpoint: %d", pipe_id, err);
			return err;
		}

		data->pipes[pipe_id].open = true;

		/* Spawn interpreter thread for data pipes (not pipe 0) */
		if (pipe_id > 0 && data->ctx != NULL && data->pipe_threads != NULL) {
			struct iio_usb_pipe *pipe = &data->pipes[pipe_id];
			int idx = pipe_id - 1;

			/* The receive side is carried by the session number, so only
			 * the transmit side needs clearing here.
			 */
			pipe->tx_err = 0;
			k_sem_reset(&pipe->tx_sem);

			k_thread_create(&data->pipe_threads[idx],
					data->pipe_stacks +
						idx * CONFIG_LIBIIO_IIOD_USB_PIPE_THREAD_STACK_SIZE,
					CONFIG_LIBIIO_IIOD_USB_PIPE_THREAD_STACK_SIZE,
					pipe_interpreter_thread, pipe, data,
					(void *)(intptr_t)pipe_id,
					CONFIG_LIBIIO_IIOD_USB_THREAD_PRIORITY, 0, K_NO_WAIT);
#ifdef CONFIG_THREAD_NAME
			{
				char name[CONFIG_THREAD_MAX_NAME_LEN];

				snprintf(name, sizeof(name), "iiod_usb_pipe%u", pipe_id);
				k_thread_name_set(&data->pipe_threads[idx], name);
			}
#endif
			LOG_INF("Pipe %u: interpreter thread spawned", pipe_id);
		}
		break;

	case IIO_USD_CMD_CLOSE_PIPE:
		LOG_INF("CLOSE_PIPE %u", pipe_id);
		if (pipe_id >= data->num_pipes) {
			return -EINVAL;
		}
		/*
		 * The buffer stays armed: dequeuing a bulk OUT wedges the controller.
		 * It keeps its credit too, so reopen doesn't double-arm.
		 */
		if (data->pipes[pipe_id].open) {
			data->pipes[pipe_id].open = false;
			iio_usb_pipe_fail_rx(&data->pipes[pipe_id], -ESHUTDOWN);
		}
		break;

	default:
		LOG_DBG("Unknown vendor request 0x%02x", request);
		break;
	}

	return 0;
}

/*
 * Check the endpoint map the stack settled on, and log it.
 *
 * This is the first point it can be checked: the bus speed, which decides
 * the live descriptor array, is only known once the bus has reset. A pipe
 * whose pair didn't get assigned would otherwise show up only as every
 * completion silently dropped by find_pipe_by_ep().
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

static void iio_usb_enable(struct usbd_class_data *const c_data)
{
	struct iio_usb_data *data = usbd_class_get_private(c_data);

	LOG_INF("IIO USB class enabled");

	if (iio_usb_validate_eps(c_data)) {
		/*
		 * Leave the class disabled rather than arming a map that cannot
		 * work. Every OPEN_PIPE then stalls, which the host reports at the
		 * request, instead of the pipes silently never answering.
		 */
		LOG_ERR("IIO USB class left disabled: unusable endpoint map");
		return;
	}

	data->enabled = true;

	/*
	 * Buffers claimed here live for the whole configuration - open and close
	 * never release one. Credit is reconciled first, since in-flight
	 * completions from before this event may still return some.
	 */
	data->pipes[0].open = true;
	for (int i = 0; i < data->num_pipes; i++) {
		struct iio_usb_pipe *pipe = &data->pipes[i];

		k_sem_reset(&pipe->rx_credit);
		for (int n = 0; n < data->rx_depth; n++) {
			k_sem_give(&pipe->rx_credit);
		}

		for (int n = 0; n < data->rx_depth; n++) {
			if (iio_usb_arm_rx(c_data, pipe)) {
				LOG_ERR("Pipe %d: failed to arm receive endpoint", i);
				break;
			}
		}
	}

	/* Signal that USB is ready for IIOD interpreter */
	k_sem_give(&data->enabled_sem);
	LOG_INF("USB ready - signaled iiod_interpreter to start");
}

static void iio_usb_disable(struct usbd_class_data *const c_data)
{
	struct iio_usb_data *data = usbd_class_get_private(c_data);

	LOG_INF("IIO USB class disabled");
	data->enabled = false;

	/* Signal all open pipes to exit their interpreter loops */
	for (int i = 0; i < data->num_pipes; i++) {
		/*
		 * The stack disabled and dequeued the endpoints before calling this,
		 * so every buffer returns on its own completion, queued behind this
		 * event. Credit is restored there and reconciled at the next enable.
		 */
		if (data->pipes[i].open) {
			data->pipes[i].open = false;
			iio_usb_pipe_fail_rx(&data->pipes[i], -ESHUTDOWN);
			k_sem_give(&data->pipes[i].tx_sem);
		}
	}
}

static const struct usbd_class_api iio_usb_api = {
	.get_desc = iio_usb_get_desc,
	.control_to_dev = iio_usb_control_to_dev,
	.request = iio_usb_request_handler,
	.enable = iio_usb_enable,
	.disable = iio_usb_disable,
	.init = iio_usb_init,
};

/*
 * Main IIOD interpreter thread. Runs the command-channel interpreter
 * on pipe 0, restarting on USB disconnect/reconnect.
 */
static void iiod_usb_thread_fn(struct iio_usb_data *data)
{
	struct iio_context_params ctx_params = {0};

	while (true) {
		LOG_INF("Initializing tinyiiod resources...");
		if (iiod_init() < 0) {
			LOG_ERR("Failed to initialize tinyiiod resources");
			return;
		}

		LOG_INF("Creating shared IIO context...");
		data->ctx = iio_create_context(&ctx_params, "zephyr:");
		if (iio_err(data->ctx)) {
			LOG_ERR("Context creation failed");
			iiod_cleanup();
			return;
		}

		LOG_INF("Getting xml data");
		data->xml = iio_context_get_xml(data->ctx);
		if (iio_err(data->xml)) {
			LOG_ERR("Error getting context XML");
			iio_context_destroy(data->ctx);
			iiod_cleanup();
			return;
		}

		data->xml_len = strlen(data->xml) + 1;
		LOG_INF("XML ready, length: %zu bytes", data->xml_len);

		/* Wait for USB to be enabled before starting interpreter */
		LOG_INF("Waiting for USB enumeration and configuration...");
		k_sem_take(&data->enabled_sem, K_FOREVER);
		LOG_INF("USB is ready!");

		/* Give pipe 0 a fresh session so the interpreter cannot inherit
		 * anything queued before the bus came up.
		 */
		iio_usb_pipe_new_session(&data->pipes[0]);
		data->pipes[0].tx_err = 0;
		k_sem_reset(&data->pipes[0].tx_sem);

		LOG_INF("Starting IIOD interpreter on pipe 0");

		/* Pipe 0 runs the command-channel interpreter in this thread */
		iiod_interpreter(data->ctx, (struct iiod_pdata *)&data->pipes[0],
				 iiod_usb_pipe_read, iiod_usb_pipe_write, data->xml,
				 data->xml_len);

		LOG_INF("IIOD interpreter exited, cleaning up...");

		if (data->pipe_threads != NULL) {
			for (int i = 0; i < data->num_pipes - 1; i++) {
				k_thread_join(&data->pipe_threads[i], K_MSEC(1000));
			}
		}

		free((void *)data->xml);
		data->xml = NULL;
		iio_context_destroy(data->ctx);
		data->ctx = NULL;
		iiod_cleanup();

		LOG_INF("Waiting for USB reconnect...");
	}
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
BUILD_ASSERT(DT_INST_PROP(inst, rx_buf_size) % USBD_MAX_BULK_MPS == 0,                             \
	"node " DT_NODE_PATH(DT_DRV_INST(inst))                                                    \
	" rx-buf-size is not a multiple of the bulk endpoint max packet size");                    \
                                                                                                   \
UDC_BUF_POOL_DEFINE(iio_usb_rx_pool_##inst,                                                        \
		    DT_INST_PROP(inst, num_pipes) * DT_INST_PROP(inst, rx_depth),                  \
		    DT_INST_PROP(inst, rx_buf_size), sizeof(struct iio_usb_buf_info), NULL);       \
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
	.enabled_sem = Z_SEM_INITIALIZER(iio_usb_data_##inst.enabled_sem, 0, 1),                   \
	.num_pipes = DT_INST_PROP(inst, num_pipes),                                                \
	.rx_buf_size = DT_INST_PROP(inst, rx_buf_size),                                            \
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
