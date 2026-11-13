/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Pipe lifecycle tests for the IIO USB class. Both ends of the bus are in this
 * image, so these run on native_sim with no hardware, no USB/IP and no root.
 * The vendor requests and the handshake are what the host-side libiio backend
 * sends, so a pipe that answers here answers a real client.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/usbh.h>
#include <zephyr/drivers/usb/uhc.h>

#include <usbh_ch9.h>
#include <usbh_device.h>

#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(iio_usb_class_test, LOG_LEVEL_INF);

/* Vendor control requests, as the host-side libiio backend numbers them */
#define IIO_USD_CMD_RESET_PIPES 0
#define IIO_USD_CMD_OPEN_PIPE   1
#define IIO_USD_CMD_CLOSE_PIPE  2

/* VENDOR | RECIPIENT_INTERFACE, host to device */
#define IIO_VENDOR_REQTYPE 0x41U

/* Binary-protocol opcodes, as iiod-responder.h numbers them */
#define IIOD_OP_RESPONSE 0
#define IIOD_OP_PRINT    1

struct iiod_wire_command {
	uint16_t client_id;
	uint8_t op;
	uint8_t dev;
	int32_t code;
} __packed;

BUILD_ASSERT(sizeof(struct iiod_wire_command) == 8, "iiod command header must be 8 bytes");

/* The class puts its endpoints on interface 0, which is what wIndex names */
#define IIO_IFACE 0U

#define IIO_NUM_PIPES DT_PROP(DT_NODELABEL(iio_usb0), num_pipes)

/* One transmit chunk: the most the class sends per USB transfer */
#define IIO_TX_CHUNK DT_PROP(DT_NODELABEL(iio_usb0), tx_buf_size)

BUILD_ASSERT(IIO_NUM_PIPES >= 3,
	     "the tests need a command channel and two data pipes");

/* Long enough that a timeout means a pipe is not answering, not a slow bus */
#define PIPE_TIMEOUT K_MSEC(500)

/* More cycles than any pipe has receive buffers, so a transport leaking one
 * per cycle would have run out well before the end.
 */
#define REUSE_CYCLES 8

USBH_CONTROLLER_DEFINE(uhs_ctx, DEVICE_DT_GET(DT_NODELABEL(zephyr_uhc0)));

USBD_DEVICE_DEFINE(test_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x0456, 0xb673);

USBD_DESC_LANG_DEFINE(test_lang);
USBD_DESC_MANUFACTURER_DEFINE(test_mfr, "Analog Devices Inc");
USBD_DESC_PRODUCT_DEFINE(test_product, "IIO USB Device");

static const uint8_t test_attributes = USB_SCD_SELF_POWERED;

USBD_CONFIGURATION_DEFINE(test_fs_config, test_attributes, 125, NULL);
IF_ENABLED(USBD_SUPPORTS_HIGH_SPEED, (
	USBD_CONFIGURATION_DEFINE(test_hs_config, test_attributes, 125, NULL);
))

static struct usb_device *udev;
static uint8_t ep_in[IIO_NUM_PIPES];
static uint8_t ep_out[IIO_NUM_PIPES];

static K_SEM_DEFINE(bulk_done, 0, 1);
static int bulk_err;

static int bulk_cb(struct usb_device *const dev, struct uhc_transfer *const xfer)
{
	ARG_UNUSED(dev);

	bulk_err = xfer->err;
	k_sem_give(&bulk_done);

	return 0;
}

/* One bulk transfer, waited out; only one runs at a time */
static int bulk_xfer(const uint8_t ep, void *const data, const size_t len, size_t *const got,
		     const k_timeout_t timeout)
{
	struct uhc_transfer *xfer;
	struct net_buf *buf;
	int err;

	k_sem_reset(&bulk_done);
	bulk_err = 0;

	xfer = usbh_xfer_alloc(udev, ep, bulk_cb, NULL);
	if (xfer == NULL) {
		return -ENOMEM;
	}

	buf = usbh_xfer_buf_alloc(udev, len);
	if (buf == NULL) {
		uhc_xfer_unref(xfer);
		return -ENOMEM;
	}

	if (USB_EP_DIR_IS_OUT(ep)) {
		net_buf_add_mem(buf, data, len);
	}

	err = usbh_xfer_buf_add(udev, xfer, buf);
	if (err) {
		goto out;
	}

	err = usbh_xfer_enqueue(udev, xfer);
	if (err) {
		goto out;
	}

	if (k_sem_take(&bulk_done, timeout)) {
		usbh_xfer_dequeue(udev, xfer);
		err = -ETIMEDOUT;
		goto out;
	}

	err = bulk_err;
	if (err == 0 && USB_EP_DIR_IS_IN(ep)) {
		memcpy(data, buf->data, MIN(len, buf->len));
		if (got != NULL) {
			*got = buf->len;
		}
	}

out:
	usbh_xfer_buf_free(udev, buf);
	uhc_xfer_unref(xfer);

	return err;
}

static int vendor_req(const uint8_t request, const uint16_t value)
{
	return usbh_req_setup(udev, IIO_VENDOR_REQTYPE, request, value, IIO_IFACE, 0, NULL);
}

/*
 * Every pipe opens with "BINARY\r\n" and an ASCII integer line back - the one
 * point at which a pipe left bare, or holding stale bytes, fails.
 */
static void expect_handshake(const unsigned int pipe)
{
	char reply[16] = {0};
	size_t got = 0;

	zassert_ok(bulk_xfer(ep_out[pipe], "BINARY\r\n", 8, NULL, PIPE_TIMEOUT),
		   "pipe %u: handshake write failed", pipe);

	zassert_ok(bulk_xfer(ep_in[pipe], reply, sizeof(reply) - 1, &got, PIPE_TIMEOUT),
		   "pipe %u: handshake read failed", pipe);
	zassert_true(got >= 2, "pipe %u: handshake reply was %zu bytes", pipe, got);
	zassert_equal(reply[0], '0', "pipe %u: handshake reply was \"%s\"", pipe, reply);
}

static void open_pipe(const unsigned int pipe)
{
	zassert_ok(vendor_req(IIO_USD_CMD_OPEN_PIPE, pipe), "OPEN_PIPE %u failed", pipe);
}

static void close_pipe(const unsigned int pipe)
{
	zassert_ok(vendor_req(IIO_USD_CMD_CLOSE_PIPE, pipe), "CLOSE_PIPE %u failed", pipe);
}

/*
 * Read the endpoint map out of the configuration descriptor rather than
 * assuming it: MAX32 numbers its IN endpoints from a different base than its
 * OUT ones. The assertions mirror the real backend's own requirements - pairs
 * in descriptor order, IN before OUT, all bulk.
 */
static void discover_endpoints(void)
{
	const uint8_t *const desc = (const uint8_t *)udev->cfg_desc;
	const struct usb_cfg_descriptor *const cfg = (const void *)desc;
	uint16_t total = sys_le16_to_cpu(cfg->wTotalLength);
	unsigned int found = 0;
	uint16_t off = 0;

	while (off + 2U <= total) {
		const uint8_t len = desc[off];
		const uint8_t type = desc[off + 1U];

		if (len == 0U) {
			break;
		}

		if (type == USB_DESC_ENDPOINT) {
			const struct usb_ep_descriptor *const ed = (const void *)&desc[off];

			zassert_true(found < IIO_NUM_PIPES * 2U,
				     "more endpoints than %u pipes need", IIO_NUM_PIPES);
			zassert_equal(ed->bmAttributes, USB_EP_TYPE_BULK,
				      "endpoint %u is not bulk", found);

			if (found % 2U == 0U) {
				zassert_true(USB_EP_DIR_IS_IN(ed->bEndpointAddress),
					     "endpoint %u should be IN", found);
				ep_in[found / 2U] = ed->bEndpointAddress;
			} else {
				zassert_true(USB_EP_DIR_IS_OUT(ed->bEndpointAddress),
					     "endpoint %u should be OUT", found);
				ep_out[found / 2U] = ed->bEndpointAddress;
			}

			found++;
		}

		off += len;
	}

	zassert_equal(found, IIO_NUM_PIPES * 2U,
		      "found %u endpoints, expected %u", found, IIO_NUM_PIPES * 2U);

	for (unsigned int i = 0; i < IIO_NUM_PIPES; i++) {
		LOG_INF("pipe %u: IN=0x%02x OUT=0x%02x", i, ep_in[i], ep_out[i]);
	}
}

static void *iio_usb_class_setup(void)
{
	zassert_ok(usbh_init(&uhs_ctx), "usbh_init failed");
	zassert_ok(usbh_enable(&uhs_ctx), "usbh_enable failed");
	zassert_ok(uhc_bus_reset(uhs_ctx.dev), "bus reset failed");
	zassert_ok(uhc_bus_resume(uhs_ctx.dev), "bus resume failed");
	zassert_ok(uhc_sof_enable(uhs_ctx.dev), "SoF enable failed");

	/*
	 * Only now bring the device up: the virtual bus reports the connection
	 * when the device is enabled, so doing this before the host was
	 * listening would lose the event.
	 */
	zassert_ok(usbd_add_descriptor(&test_usbd, &test_lang),
		   "failed to add language descriptor");
	zassert_ok(usbd_add_descriptor(&test_usbd, &test_mfr),
		   "failed to add manufacturer descriptor");
	zassert_ok(usbd_add_descriptor(&test_usbd, &test_product),
		   "failed to add product descriptor");
	zassert_ok(usbd_add_configuration(&test_usbd, USBD_SPEED_FS, &test_fs_config),
		   "failed to add FS configuration");
	zassert_ok(usbd_register_all_classes(&test_usbd, USBD_SPEED_FS, 1, NULL),
		   "failed to register FS classes");

	IF_ENABLED(USBD_SUPPORTS_HIGH_SPEED, (
		if (usbd_caps_speed(&test_usbd) == USBD_SPEED_HS) {
			zassert_ok(usbd_add_configuration(&test_usbd, USBD_SPEED_HS,
							  &test_hs_config),
				   "failed to add HS configuration");

			zassert_ok(usbd_register_all_classes(&test_usbd, USBD_SPEED_HS, 1, NULL),
				   "failed to register HS classes");
		}
	))

	zassert_ok(usbd_init(&test_usbd), "usbd_init failed");
	zassert_ok(usbd_enable(&test_usbd), "usbd_enable failed");

	/* Let the host enumerate it and set a configuration, which arms the pipes */
	k_msleep(500);

	udev = usbh_device_get_any(&uhs_ctx);
	zassert_not_null(udev, "no USB device enumerated");
	zassert_not_null(udev->cfg_desc, "device was never configured");

	discover_endpoints();

	return NULL;
}

/* Start every test from the state a fresh client finds: the first thing the
 * host backend sends when it creates a context.
 */
static void iio_usb_class_before(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_ok(vendor_req(IIO_USD_CMD_RESET_PIPES, 0), "RESET_PIPES failed");
}

ZTEST_SUITE(iio_usb_class, NULL, iio_usb_class_setup, iio_usb_class_before, NULL, NULL);

/* The sequence a context creation performs, end to end */
ZTEST(iio_usb_class, test_command_channel_answers_after_reset)
{
	open_pipe(0);
	expect_handshake(0);
}

/* A data pipe is opened per buffer, and completes the same handshake */
ZTEST(iio_usb_class, test_data_pipe_answers_when_opened)
{
	open_pipe(0);
	expect_handshake(0);

	open_pipe(1);
	expect_handshake(1);
}

/* The host sends OPEN_PIPE 0 even when the class already considers it open */
ZTEST(iio_usb_class, test_reopening_an_open_pipe_is_accepted)
{
	open_pipe(0);
	open_pipe(0);
	expect_handshake(0);
}

/* Out of range has to be refused, and must not take the device with it */
ZTEST(iio_usb_class, test_out_of_range_open_is_refused)
{
	zassert_not_ok(vendor_req(IIO_USD_CMD_OPEN_PIPE, IIO_NUM_PIPES),
		       "OPEN_PIPE %u was accepted", IIO_NUM_PIPES);

	open_pipe(0);
	expect_handshake(0);
}

/* Closing a pipe that was never opened is something a client teardown does */
ZTEST(iio_usb_class, test_closing_a_closed_pipe_is_accepted)
{
	close_pipe(1);
	close_pipe(1);

	open_pipe(1);
	expect_handshake(1);
}

/* An unknown request must be stalled, not wedge the control endpoint */
ZTEST(iio_usb_class, test_unknown_vendor_request_does_not_wedge)
{
	zassert_not_ok(vendor_req(0x7fU, 0), "unknown vendor request was accepted");

	open_pipe(0);
	expect_handshake(0);
}

/*
 * A reopened pipe must not inherit the previous session's bytes - what an
 * abandoned client leaves behind - or the interpreter parses the leftovers as
 * a command header.
 */
ZTEST(iio_usb_class, test_reopened_pipe_discards_the_previous_session)
{
	open_pipe(1);
	expect_handshake(1);

	zassert_ok(bulk_xfer(ep_out[1], "\x01\x02\x03", 3, NULL, PIPE_TIMEOUT),
		   "partial frame write failed");

	close_pipe(1);
	open_pipe(1);
	expect_handshake(1);
}

/*
 * A response larger than one transmit chunk has to arrive whole. Nothing else
 * in this suite reads more than a single chunk, so without this a transmit that
 * gave up early would truncate every large reply unnoticed.
 */
ZTEST(iio_usb_class, test_multi_chunk_response_arrives_whole)
{
	const struct iiod_wire_command print_cmd = {
		.client_id = 1,
		.op = IIOD_OP_PRINT,
	};
	struct iiod_wire_command rsp = {0};
	uint8_t chunk[IIO_TX_CHUNK];
	size_t total = 0;
	size_t got = 0;
	bool first = true;

	open_pipe(1);
	expect_handshake(1);

	zassert_ok(bulk_xfer(ep_out[1], (void *)&print_cmd, sizeof(print_cmd), NULL, PIPE_TIMEOUT),
		   "PRINT write failed");

	zassert_ok(bulk_xfer(ep_in[1], &rsp, sizeof(rsp), &got, PIPE_TIMEOUT),
		   "response header read failed");
	zassert_equal(rsp.op, IIOD_OP_RESPONSE, "reply was not a response (op %u)", rsp.op);
	zassert_true(rsp.code > (int32_t)IIO_TX_CHUNK,
		     "XML is %d bytes, which does not span chunks", rsp.code);

	while (total < (size_t)rsp.code) {
		const size_t want = MIN(sizeof(chunk), (size_t)rsp.code - total);

		got = 0;
		zassert_ok(bulk_xfer(ep_in[1], chunk, want, &got, PIPE_TIMEOUT),
			   "payload read failed at offset %zu", total);
		zassert_true(got > 0, "payload read returned nothing at offset %zu", total);

		if (first) {
			/* Enough to show it is the XML and not a stale frame */
			zassert_mem_equal(chunk, "<?xml", 5,
					  "payload does not begin with an XML declaration");
			first = false;
		}

		total += got;
	}

	zassert_equal(total, (size_t)rsp.code, "read %zu payload bytes, header said %d", total,
		      rsp.code);

	/* The pipe is back at a frame boundary, so it still answers */
	close_pipe(1);
	open_pipe(1);
	expect_handshake(1);
}

/*
 * The transmit-side mirror: a session that ends part-way through a response
 * must not go on delivering it to the next session. The chunk already handed
 * to the controller cannot be abandoned, so the next session is expected to
 * see it; the chunk after it is only sent if the writer never rechecks.
 */
ZTEST(iio_usb_class, test_reopened_pipe_does_not_inherit_a_partial_response)
{
	const struct iiod_wire_command print_cmd = {
		.client_id = 1,
		.op = IIOD_OP_PRINT,
	};
	struct iiod_wire_command rsp = {0};
	uint8_t reply[IIO_TX_CHUNK];
	size_t got = 0;

	open_pipe(1);
	expect_handshake(1);

	zassert_ok(bulk_xfer(ep_out[1], (void *)&print_cmd, sizeof(print_cmd), NULL, PIPE_TIMEOUT),
		   "PRINT write failed");

	/* Read the response header only, leaving the payload write under way */
	zassert_ok(bulk_xfer(ep_in[1], &rsp, sizeof(rsp), &got, PIPE_TIMEOUT),
		   "response header read failed");
	zassert_equal(got, sizeof(rsp), "response header was %zu bytes", got);
	zassert_equal(rsp.op, IIOD_OP_RESPONSE, "reply was not a response (op %u)", rsp.op);
	zassert_true(rsp.code > (int32_t)IIO_TX_CHUNK,
		     "XML is %d bytes, not more than one %u-byte chunk - this case needs a "
		     "response that spans chunks", rsp.code, IIO_TX_CHUNK);

	/* End the session mid-response and start a fresh one */
	close_pipe(1);
	open_pipe(1);

	/* The new session's handshake, which arrives stamped for it */
	zassert_ok(bulk_xfer(ep_out[1], "BINARY\r\n", 8, NULL, PIPE_TIMEOUT),
		   "handshake write failed");

	/* The chunk already in the controller's hands; unavoidable, so expected */
	zassert_ok(bulk_xfer(ep_in[1], reply, sizeof(reply), &got, PIPE_TIMEOUT),
		   "read of the in-flight chunk failed");

	/*
	 * This read must be the new session's handshake reply. Another full
	 * chunk means the dead session is still writing.
	 */
	got = 0;
	memset(reply, 0, sizeof(reply));
	zassert_ok(bulk_xfer(ep_in[1], reply, sizeof(reply), &got, PIPE_TIMEOUT),
		   "handshake read failed");
	zassert_equal(got, 3, "expected the 3-byte handshake reply, got %zu bytes of the "
		      "previous session's response", got);
	zassert_equal(reply[0], '0', "handshake reply was \"%s\"", reply);
}

/*
 * The same, for bytes that arrive while the pipe is closed. The class cannot
 * hand its receive buffer back on close - dequeuing a bulk OUT wedges the MAX32
 * controller - so a packet sent now must be dropped, not delivered.
 */
ZTEST(iio_usb_class, test_bytes_sent_to_a_closed_pipe_are_dropped)
{
	open_pipe(1);
	expect_handshake(1);
	close_pipe(1);

	zassert_ok(bulk_xfer(ep_out[1], "\x04\x05\x06", 3, NULL, PIPE_TIMEOUT),
		   "write to closed pipe failed");

	open_pipe(1);
	expect_handshake(1);
}

/*
 * A leaked receive buffer stays invisible on the pipe that is cycling and only
 * surfaces on the next pipe opened, which finds the pool empty. Hence two
 * pipes: one to drain it, one to find it drained.
 */
ZTEST(iio_usb_class, test_cycling_one_pipe_leaves_another_usable)
{
	open_pipe(0);
	expect_handshake(0);

	for (unsigned int cycle = 0; cycle < REUSE_CYCLES; cycle++) {
		open_pipe(1);
		expect_handshake(1);
		close_pipe(1);
	}

	open_pipe(2);
	expect_handshake(2);
}

/*
 * SET_INTERFACE disables, dequeues and re-enables every endpoint, telling the
 * class only through .update. libiio's own backend never triggers this, but
 * libusb's set_interface_alt_setting does. Ending the queued buffers' sessions
 * is recoverable, since the host reopens the pipe; endpoints left bare are not.
 */
ZTEST(iio_usb_class, test_pipes_recover_from_set_interface)
{
	open_pipe(0);
	expect_handshake(0);

	zassert_ok(usbh_req_set_alt(udev, IIO_IFACE, 0), "SET_INTERFACE failed");

	open_pipe(0);
	expect_handshake(0);

	open_pipe(1);
	expect_handshake(1);
}

/*
 * A host may drop the configuration and set it again - what a replug looks
 * like to the device - and the pipes have to come back.
 */
ZTEST(iio_usb_class, test_pipes_come_back_after_reconfiguration)
{
	open_pipe(0);
	expect_handshake(0);

	zassert_ok(usbh_req_set_cfg(udev, 0), "could not clear the configuration");

	zassert_ok(usbh_req_set_cfg(udev, 1), "could not set the configuration");

	/* Let the dequeue completions and the class's re-arming settle */
	k_msleep(100);

	open_pipe(0);
	expect_handshake(0);

	open_pipe(1);
	expect_handshake(1);
}

/*
 * A reopen can arrive before the previous interpreter has finished unwinding,
 * so the class must accept the new session without waiting for the old one.
 */
ZTEST(iio_usb_class, test_reopen_without_pause_is_served)
{
	open_pipe(1);
	expect_handshake(1);

	for (unsigned int cycle = 0; cycle < REUSE_CYCLES; cycle++) {
		close_pipe(1);
		open_pipe(1);
	}

	expect_handshake(1);
}

/*
 * The host never sends CLOSE_PIPE 0, so RESET_PIPES is the only signal that
 * returns pipe 0 to a frame boundary. Until reopened it must answer nothing,
 * or the next client's handshake is read as the remainder of a dead command.
 */
ZTEST(iio_usb_class, test_reset_closes_the_command_channel)
{
	char reply[4] = {0};
	int err;

	open_pipe(0);
	expect_handshake(0);

	zassert_ok(vendor_req(IIO_USD_CMD_RESET_PIPES, 0), "RESET_PIPES failed");

	/* Accepted by the endpoint, which stays armed, and dropped by the class */
	zassert_ok(bulk_xfer(ep_out[0], "BINARY\r\n", 8, NULL, PIPE_TIMEOUT),
		   "write to the closed command channel failed");

	err = bulk_xfer(ep_in[0], reply, sizeof(reply) - 1, NULL, K_MSEC(100));
	zassert_equal(err, -ETIMEDOUT, "closed command channel answered (%d)", err);

	open_pipe(0);
	expect_handshake(0);
}

/* On context destroy, RESET_PIPES arrives after the host stops reading */
ZTEST(iio_usb_class, test_command_channel_survives_repeated_resets)
{
	for (unsigned int i = 0; i < 3; i++) {
		zassert_ok(vendor_req(IIO_USD_CMD_RESET_PIPES, 0), "RESET_PIPES %u failed", i);
		open_pipe(0);
		expect_handshake(0);
	}
}
