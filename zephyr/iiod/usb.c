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
#include <zephyr/sys/ring_buffer.h>
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
 * Per-pipe state. Each pipe has its own RX FIFO and TX semaphore,
 * and its own endpoint addresses.
 *
 * The RX path uses a ring buffer (byte-stream FIFO) so that multiple USB
 * packets arriving while the interpreter is busy are queued rather
 * than overwritten.
 */
struct iio_usb_pipe {
	uint8_t ep_in;  /* IN endpoint address (e.g., 0x81) */
	uint8_t ep_out; /* OUT endpoint address (e.g., 0x01) */
	struct ring_buf rx_ringbuf;
	struct k_sem rx_sem; /* signaled when data is added to ring buffer */
	struct k_sem tx_sem;
	int rx_err;
	int tx_err;
	bool open; /* Whether this pipe has been opened by host */
	struct iio_usb_data *data; /* back-pointer to parent */
};

struct iio_usb_data {
	struct iio_usb_desc *const desc;
	const struct usb_desc_header **const fs_desc;
	const struct usb_desc_header **const hs_desc;
	struct usbd_desc_node *const iface_str_desc;
	struct usbd_class_data *c_data;
	struct k_sem enabled_sem;
	uint8_t num_pipes;
	uint16_t rx_buf_size;
	uint16_t tx_buf_size;
	uint16_t rx_fifo_size;
	uint8_t *rx_fifo_data;
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
 * Find the pipe that owns a given endpoint address.
 * Returns the pipe index or -1 if not found.
 */
static int find_pipe_by_ep(struct iio_usb_data *data, uint8_t ep_addr)
{
	for (int i = 0; i < data->num_pipes; i++) {
		if (data->pipes[i].ep_in == ep_addr || data->pipes[i].ep_out == ep_addr) {
			return i;
		}
	}
	return -1;
}

/*
 * Get the correct IN/OUT endpoint address for a pipe, accounting for
 * bus speed (FS vs HS). Since both FS and HS descriptors use the same
 * endpoint addresses (just different max packet sizes), we can just
 * return the pipe's stored address directly.
 */
static inline uint8_t pipe_get_bulk_in(struct iio_usb_pipe *pipe)
{
	return pipe->ep_in;
}

static inline uint8_t pipe_get_bulk_out(struct iio_usb_pipe *pipe)
{
	return pipe->ep_out;
}

/* Queue a receive buffer on a specific pipe's OUT endpoint */
static int iio_usb_queue_rx_pipe(struct usbd_class_data *const c_data, struct iio_usb_pipe *pipe)
{
	struct iio_usb_data *data = usbd_class_get_private(c_data);
	struct net_buf *buf;
	int err;

	if (!data->enabled) {
		return -ENODEV;
	}

	buf = usbd_ep_buf_alloc(c_data, pipe_get_bulk_out(pipe), data->rx_buf_size);
	if (buf == NULL) {
		LOG_ERR("Pipe 0x%02x: Failed to allocate RX buffer", pipe->ep_out);
		return -ENOMEM;
	}

	err = usbd_ep_enqueue(c_data, buf);
	if (err) {
		LOG_ERR("Pipe 0x%02x: Failed to enqueue RX buffer: %d", pipe->ep_out, err);
		net_buf_unref(buf);
		return err;
	}

	LOG_DBG("Pipe 0x%02x: RX buffer queued", pipe->ep_out);
	return 0;
}

static void *iio_usb_get_desc(struct usbd_class_data *const c_data, const enum usbd_speed speed)
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

	pipe_idx = find_pipe_by_ep(data, ep);
	if (pipe_idx < 0) {
		LOG_ERR("Unknown endpoint 0x%02x", ep);
		net_buf_unref(buf);
		return -EINVAL;
	}
	pipe = &data->pipes[pipe_idx];

	if (ep == pipe->ep_out) {
		/* Received data from host (RX) — append to FIFO */
		LOG_DBG("Pipe %d RX complete: err=%d, len=%u", pipe_idx, err, buf->len);

		if (err == 0 && buf->len > 0) {
			uint32_t written = ring_buf_put(&pipe->rx_ringbuf, buf->data, buf->len);

			if (written < buf->len) {
				pipe->rx_err = -EIO;
				LOG_ERR("Pipe %d: RX FIFO overflow, lost %u bytes", pipe_idx,
					buf->len - written);
			} else {
				pipe->rx_err = 0;
			}
			k_sem_give(&pipe->rx_sem);
			net_buf_unref(buf);

			/* Re-queue another receive buffer */
			iio_usb_queue_rx_pipe(c_data, pipe);
		} else {
			pipe->rx_err = err ? err : -EIO;
			k_sem_give(&pipe->rx_sem);
			net_buf_unref(buf);
		}

	} else if (ep == pipe->ep_in) {
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
	 * Initialize per-pipe state.
	 * Read the actual endpoint addresses from the descriptor struct —
	 * the Zephyr USB stack remaps bEndpointAddress during class
	 * registration (usbd_register_all_classes), so the addresses in
	 * the struct now reflect the real UDC endpoint addresses, not
	 * the placeholder values we put in the initializer.
	 */
	for (int i = 0; i < data->num_pipes; i++) {
		struct iio_usb_pipe *pipe = &data->pipes[i];

		pipe->ep_in = desc->if0_ep[i * 2].bEndpointAddress;
		pipe->ep_out = desc->if0_ep[i * 2 + 1].bEndpointAddress;
		pipe->data = data;
		ring_buf_init(&pipe->rx_ringbuf, data->rx_fifo_size,
			      data->rx_fifo_data + i * data->rx_fifo_size);
		k_sem_init(&pipe->rx_sem, 0, K_SEM_MAX_LIMIT);
		k_sem_init(&pipe->tx_sem, 0, 1);
		pipe->rx_err = 0;
		pipe->tx_err = 0;
		pipe->open = false;

		LOG_INF("Pipe %d: IN=0x%02x OUT=0x%02x", i, pipe->ep_in, pipe->ep_out);
	}

	k_sem_init(&data->enabled_sem, 0, 1);

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
 * Read/write callbacks for pipe interpreter threads.
 * The pdata pointer is the iio_usb_pipe struct for the pipe.
 */
static ssize_t iiod_usb_pipe_read(struct iiod_pdata *pdata, void *buf, size_t size)
{
	struct iio_usb_pipe *pipe = (struct iio_usb_pipe *)pdata;
	size_t bytes_read = 0;
	uint8_t *dest = (uint8_t *)buf;

	LOG_DBG("Pipe 0x%02x: read %zu bytes requested", pipe->ep_out, size);

	while (bytes_read < size) {
		uint32_t got =
			ring_buf_get(&pipe->rx_ringbuf, dest + bytes_read, size - bytes_read);
		bytes_read += got;

		if (bytes_read < size) {
			/* Ring buffer empty — wait for more data */
			k_sem_take(&pipe->rx_sem, K_FOREVER);

			if (pipe->rx_err) {
				/* Only log as error if pipe is still open.
				 * During pipe closure, pending USB transfers are cancelled
				 * and return -ETIMEDOUT, which is expected behavior.
				 */
				if (pipe->open) {
					LOG_ERR("Pipe 0x%02x: USB RX error: %d", pipe->ep_out,
						pipe->rx_err);
				} else {
					LOG_DBG("Pipe 0x%02x: RX error during shutdown: %d",
						pipe->ep_out, pipe->rx_err);
				}
				return pipe->rx_err;
			}
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

	LOG_DBG("Pipe 0x%02x: TX %zu bytes", pipe->ep_in, size);

	size_t bytes_sent = 0;

	while (bytes_sent < size) {
		size_t chunk_size = MIN(size - bytes_sent, data->tx_buf_size);

		net_buf = usbd_ep_buf_alloc(c_data, pipe_get_bulk_in(pipe), chunk_size);
		if (net_buf == NULL) {
			LOG_ERR("Pipe 0x%02x: Failed to allocate TX buffer for %zu bytes",
				pipe->ep_in, chunk_size);
			return bytes_sent > 0 ? bytes_sent : -ENOMEM;
		}

		net_buf_add_mem(net_buf, src + bytes_sent, chunk_size);
		pipe->tx_err = 0;

		err = usbd_ep_enqueue(c_data, net_buf);
		if (err) {
			LOG_ERR("Pipe 0x%02x: Failed to enqueue TX buffer: %d", pipe->ep_in, err);
			net_buf_unref(net_buf);
			return bytes_sent > 0 ? bytes_sent : err;
		}

		err = k_sem_take(&pipe->tx_sem, K_FOREVER);
		if (err) {
			LOG_ERR("Pipe 0x%02x: TX semaphore error: %d", pipe->ep_in, err);
			return bytes_sent > 0 ? bytes_sent : err;
		}

		if (pipe->tx_err) {
			LOG_ERR("Pipe 0x%02x: USB TX transfer failed: %d", pipe->ep_in,
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

	LOG_INF("Control to dev: bRequest=0x%02x, wValue=0x%04x, wIndex=0x%04x", request, pipe_id,
		setup->wIndex);

	switch (request) {
	case IIO_USD_CMD_RESET_PIPES:
		LOG_INF("RESET_PIPES");
		for (int i = 1; i < data->num_pipes; i++) {
			if (data->pipes[i].open) {
				data->pipes[i].open = false;
				data->pipes[i].rx_err = -ESHUTDOWN;
				k_sem_give(&data->pipes[i].rx_sem);
			}
		}
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
		data->pipes[pipe_id].open = true;

		/* Queue initial RX buffer for this pipe */
		iio_usb_queue_rx_pipe(c_data, &data->pipes[pipe_id]);

		/* Spawn interpreter thread for data pipes (not pipe 0) */
		if (pipe_id > 0 && data->ctx != NULL && data->pipe_threads != NULL) {
			struct iio_usb_pipe *pipe = &data->pipes[pipe_id];
			int idx = pipe_id - 1;

			/* Reset pipe state for clean start */
			ring_buf_reset(&pipe->rx_ringbuf);
			k_sem_reset(&pipe->rx_sem);
			pipe->rx_err = 0;
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
		if (data->pipes[pipe_id].open) {
			data->pipes[pipe_id].open = false;
			data->pipes[pipe_id].rx_err = -ESHUTDOWN;
			k_sem_give(&data->pipes[pipe_id].rx_sem);
		}
		break;

	default:
		LOG_DBG("Unknown vendor request 0x%02x", request);
		break;
	}

	return 0;
}

static void iio_usb_enable(struct usbd_class_data *const c_data)
{
	struct iio_usb_data *data = usbd_class_get_private(c_data);

	LOG_INF("IIO USB class enabled");
	data->enabled = true;

	/* Queue initial receive buffer for pipe 0 (command channel) */
	data->pipes[0].open = true;
	if (iio_usb_queue_rx_pipe(c_data, &data->pipes[0])) {
		LOG_ERR("Failed to queue initial RX buffer for pipe 0");
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
		if (data->pipes[i].open) {
			data->pipes[i].open = false;
			data->pipes[i].rx_err = -ESHUTDOWN;
			k_sem_give(&data->pipes[i].rx_sem);
			k_sem_give(&data->pipes[i].tx_sem);
		}
	}
}

static struct usbd_class_api iio_usb_api = {
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

		/* Reset pipe 0 state for clean interpreter start */
		ring_buf_reset(&data->pipes[0].rx_ringbuf);
		k_sem_reset(&data->pipes[0].rx_sem);
		data->pipes[0].rx_err = 0;
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
static const struct usb_desc_header *iio_fs_desc_##inst[] = {                                      \
	(struct usb_desc_header *)&iio_usb_desc_##inst.if0,                                        \
	LISTIFY(DT_INST_PROP(inst, num_pipes), DECLARE_FS_EP_PTR, (,), inst),                      \
	(struct usb_desc_header *)&iio_usb_desc_##inst.nil_desc,                                   \
};                                                                                                 \
                                                                                                   \
static const struct usb_desc_header *iio_hs_desc_##inst[] = {                                      \
	(struct usb_desc_header *)&iio_usb_desc_##inst.if0,                                        \
	LISTIFY(DT_INST_PROP(inst, num_pipes), DECLARE_HS_EP_PTR, (,), inst),                      \
	(struct usb_desc_header *)&iio_usb_desc_##inst.nil_desc,                                   \
};                                                                                                 \
                                                                                                   \
static uint8_t iio_usb_rx_fifo_data_##inst                                                         \
	[DT_INST_PROP(inst, num_pipes)][DT_INST_PROP(inst, rx_fifo_size)];                         \
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
	.num_pipes = DT_INST_PROP(inst, num_pipes),                                                \
	.rx_buf_size = DT_INST_PROP(inst, rx_buf_size),                                            \
	.tx_buf_size = DT_INST_PROP(inst, tx_buf_size),                                            \
	.rx_fifo_size = DT_INST_PROP(inst, rx_fifo_size),                                          \
	.rx_fifo_data = (uint8_t *)iio_usb_rx_fifo_data_##inst,                                    \
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
