/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <errno.h>
#include <no_os_delay.h>
#include <no_os_print_log.h>

#include "mxc_sys.h"
#include "mxc_errors.h"
#include "usb.h"
#include "usb_event.h"
#include "enumerate.h"
#include "nvic_table.h"

#include "iio_usb_hal.h"

#define MX_USB_MAX_PACKET_FS	0x40
#define MX_USB_MAX_PACKET_HS	0x200
#define MX_USB_STR_CHARS	31
#define MX_USB_STR_BYTES	(2 + 2 * MX_USB_STR_CHARS)

#define MX_USB_DESC_DEV_LEN	18
#define MX_USB_DESC_QUAL_LEN	10
#define MX_USB_DESC_CFG_LEN	9
#define MX_USB_DESC_IFACE_LEN	9
#define MX_USB_DESC_EP_LEN	7
#define MX_USB_DESC_CFG_MAX	(MX_USB_DESC_CFG_LEN + MX_USB_DESC_IFACE_LEN + \
				 2 * IIO_USB_MAX_PIPES * MX_USB_DESC_EP_LEN)

int usbStartupCallback(void);
int usbShutdownCallback(void);

struct mx_usb_req {
	MXC_USB_Req_t	req;
	unsigned int	pipe;
};

static __attribute__((aligned(4))) uint8_t desc_dev[MX_USB_DESC_DEV_LEN];
static __attribute__((aligned(4))) uint8_t desc_qual[MX_USB_DESC_QUAL_LEN];
static __attribute__((aligned(4))) uint8_t desc_cfg_fs[MX_USB_DESC_CFG_MAX];
static __attribute__((aligned(4))) uint8_t desc_cfg_hs[MX_USB_DESC_CFG_MAX];

static __attribute__((aligned(4))) uint8_t str_lang[4] = {
	0x04, 0x03, 0x09, 0x04
};
static __attribute__((aligned(4))) uint8_t str_mfg[MX_USB_STR_BYTES];
static __attribute__((aligned(4))) uint8_t str_prod[MX_USB_STR_BYTES];
static __attribute__((aligned(4))) uint8_t str_serial[MX_USB_STR_BYTES];
static __attribute__((aligned(4))) uint8_t str_iface[MX_USB_STR_BYTES];

static struct mx_usb_req rx_reqs[IIO_USB_MAX_PIPES];
static struct mx_usb_req tx_reqs[IIO_USB_MAX_PIPES];

static unsigned int num_pipes;
static volatile uint16_t usb_mps = MX_USB_MAX_PACKET_FS;

static uint8_t pipe_ep_in(unsigned int pipe)
{
	return (uint8_t)(2 * pipe + 1);
}

static uint8_t pipe_ep_out(unsigned int pipe)
{
	return (uint8_t)(2 * pipe + 2);
}

static bool usb_irq_lock(void)
{
	/* NVIC->ISER, not NVIC_GetEnableIRQ(): CMSIS 4 headers lack it. */
	uint32_t irq = (uint32_t)USB_IRQn;
	bool enabled = (NVIC->ISER[irq >> 5] >> (irq & 0x1f)) & 1u;

	NVIC_DisableIRQ(USB_IRQn);

	return enabled;
}

static void usb_irq_unlock(bool enabled)
{
	if (enabled)
		NVIC_EnableIRQ(USB_IRQn);
}

static void desc_put_str(uint8_t *dst, const char *src)
{
	size_t i, len = src ? strlen(src) : 0; /* Flawfinder: ignore */

	if (len > MX_USB_STR_CHARS)
		len = MX_USB_STR_CHARS;

	dst[0] = (uint8_t)(2 + 2 * len);
	dst[1] = 0x03;

	for (i = 0; i < len; i++) {
		dst[2 + 2 * i] = (uint8_t)src[i];
		dst[3 + 2 * i] = 0;
	}
}

static void desc_build_config(uint8_t *dst, unsigned int pipes, uint16_t mps)
{
	unsigned int total = MX_USB_DESC_CFG_LEN + MX_USB_DESC_IFACE_LEN +
			     2 * pipes * MX_USB_DESC_EP_LEN;
	uint8_t *ep = dst + MX_USB_DESC_CFG_LEN + MX_USB_DESC_IFACE_LEN;
	unsigned int i;

	dst[0] = MX_USB_DESC_CFG_LEN;
	dst[1] = 0x02;
	dst[2] = (uint8_t)total;
	dst[3] = (uint8_t)(total >> 8);
	dst[4] = 0x01;
	dst[5] = 0x01;
	dst[6] = 0x00;
	dst[7] = 0x80;
	dst[8] = 0xFA;

	dst += MX_USB_DESC_CFG_LEN;
	dst[0] = MX_USB_DESC_IFACE_LEN;
	dst[1] = 0x04;
	dst[2] = 0x00;
	dst[3] = 0x00;
	dst[4] = (uint8_t)(2 * pipes);
	dst[5] = 0x02;
	dst[6] = 0x00;
	dst[7] = 0x00;
	dst[8] = 0x04;

	for (i = 0; i < pipes; i++) {
		ep[0] = MX_USB_DESC_EP_LEN;
		ep[1] = 0x05;
		ep[2] = (uint8_t)(0x80 | pipe_ep_in(i));
		ep[3] = 0x02;
		ep[4] = (uint8_t)mps;
		ep[5] = (uint8_t)(mps >> 8);
		ep[6] = 0x00;
		ep += MX_USB_DESC_EP_LEN;

		ep[0] = MX_USB_DESC_EP_LEN;
		ep[1] = 0x05;
		ep[2] = pipe_ep_out(i);
		ep[3] = 0x02;
		ep[4] = (uint8_t)mps;
		ep[5] = (uint8_t)(mps >> 8);
		ep[6] = 0x00;
		ep += MX_USB_DESC_EP_LEN;
	}
}

static void desc_build(const struct iio_usb_dev_info *info)
{
	uint8_t *d = desc_dev;

	d[0] = MX_USB_DESC_DEV_LEN;
	d[1] = 0x01;
	d[2] = 0x00;
	d[3] = 0x02;
	d[4] = 0x02;
	d[5] = 0x00;
	d[6] = 0x00;
	d[7] = 0x40;
	d[8] = (uint8_t)info->vendor_id;
	d[9] = (uint8_t)(info->vendor_id >> 8);
	d[10] = (uint8_t)info->product_id;
	d[11] = (uint8_t)(info->product_id >> 8);
	d[12] = (uint8_t)info->release;
	d[13] = (uint8_t)(info->release >> 8);
	d[14] = 0x01;
	d[15] = 0x02;
	d[16] = 0x03;
	d[17] = 0x01;

	d = desc_qual;
	d[0] = MX_USB_DESC_QUAL_LEN;
	d[1] = 0x06;
	d[2] = 0x00;
	d[3] = 0x02;
	d[4] = 0x02;
	d[5] = 0x00;
	d[6] = 0x00;
	d[7] = 0x40;
	d[8] = 0x01;
	d[9] = 0x00;

	desc_build_config(desc_cfg_fs, info->num_pipes, MX_USB_MAX_PACKET_FS);
	desc_build_config(desc_cfg_hs, info->num_pipes, MX_USB_MAX_PACKET_HS);

	desc_put_str(str_mfg, info->manufacturer);
	desc_put_str(str_prod, info->product);
	desc_put_str(str_serial, info->serial);
	desc_put_str(str_iface, info->interface_name);
}

static void mx_usb_rx_cb(void *cbdata)
{
	struct mx_usb_req *r = (struct mx_usb_req *)cbdata;

	iio_usb_on_rx(r->pipe, r->req.actlen,
		      r->req.error_code ? -EIO : 0);
}

static void mx_usb_tx_cb(void *cbdata)
{
	struct mx_usb_req *r = (struct mx_usb_req *)cbdata;

	iio_usb_on_tx(r->pipe, r->req.actlen,
		      r->req.error_code ? -EIO : 0);
}

static int mx_usb_submit_rx(unsigned int pipe, void *buf, uint32_t len)
{
	struct mx_usb_req *r = &rx_reqs[pipe];
	bool locked;
	int ret;

	r->pipe = pipe;
	r->req.ep = pipe_ep_out(pipe);
	r->req.data = buf;
	r->req.reqlen = len;
	r->req.actlen = 0;
	r->req.error_code = 0;
	r->req.callback = mx_usb_rx_cb;
	r->req.cbdata = r;
	r->req.type = MAXUSB_TYPE_PKT;

	locked = usb_irq_lock();
	ret = MXC_USB_ReadEndpoint(&r->req);
	usb_irq_unlock(locked);

	return ret ? -EIO : 0;
}

static int mx_usb_submit_tx(unsigned int pipe, const void *buf, uint32_t len)
{
	struct mx_usb_req *r = &tx_reqs[pipe];
	bool locked;
	int ret;

	r->pipe = pipe;
	r->req.ep = pipe_ep_in(pipe);
	r->req.data = (uint8_t *)buf;
	r->req.reqlen = len;
	r->req.actlen = 0;
	r->req.error_code = 0;
	r->req.callback = mx_usb_tx_cb;
	r->req.cbdata = r;
	r->req.type = MAXUSB_TYPE_TRANS;

	locked = usb_irq_lock();
	ret = MXC_USB_WriteEndpoint(&r->req);
	usb_irq_unlock(locked);

	return ret ? -EIO : 0;
}

static void mx_usb_cancel(unsigned int pipe)
{
	MXC_USB_Req_t *r;
	bool locked;

	locked = usb_irq_lock();

	r = MXC_USB_GetRequest(pipe_ep_out(pipe));
	if (r)
		MXC_USB_RemoveRequest(r);

	r = MXC_USB_GetRequest(pipe_ep_in(pipe));
	if (r)
		MXC_USB_RemoveRequest(r);

	usb_irq_unlock(locked);
}

static void mx_usb_flush(unsigned int pipe)
{
	bool locked = usb_irq_lock();
	uint32_t index = MXC_USBHS->index;

	MXC_USBHS->index = pipe_ep_in(pipe);
	if (MXC_USBHS->incsrl & MXC_F_USBHS_INCSRL_INPKTRDY)
		MXC_USBHS->incsrl = MXC_F_USBHS_INCSRL_FLUSHFIFO;

	MXC_USBHS->index = pipe_ep_out(pipe);
	if (MXC_USBHS->outcsrl & MXC_F_USBHS_OUTCSRL_OUTPKTRDY)
		MXC_USBHS->outcsrl = MXC_F_USBHS_OUTCSRL_FLUSHFIFO;

	MXC_USBHS->index = index;

	usb_irq_unlock(locked);
}

static uint16_t mx_usb_max_packet(void)
{
	return usb_mps;
}

static void mx_usb_select_speed(void)
{
	if (MXC_USB_GetStatus() & MAXUSB_STATUS_HIGH_SPEED) {
		usb_mps = MX_USB_MAX_PACKET_HS;
		enum_register_descriptor(ENUM_DESC_CONFIG, desc_cfg_hs, 0);
		enum_register_descriptor(ENUM_DESC_OTHER, desc_cfg_fs, 0);
	} else {
		usb_mps = MX_USB_MAX_PACKET_FS;
		enum_register_descriptor(ENUM_DESC_CONFIG, desc_cfg_fs, 0);
		enum_register_descriptor(ENUM_DESC_OTHER, desc_cfg_hs, 0);
	}
}

static int mx_usb_event_cb(maxusb_event_t evt, void *data)
{
	switch (evt) {
	case MAXUSB_EVENT_NOVBUS:
		pr_info("USB: NOVBUS (cable disconnect)\n");
		MXC_USB_EventDisable(MAXUSB_EVENT_BRST);
		MXC_USB_EventDisable(MAXUSB_EVENT_SUSP);
		MXC_USB_EventDisable(MAXUSB_EVENT_DPACT);
		MXC_USB_Disconnect();
		enum_clearconfig();
		iio_usb_on_event(IIO_USB_EVENT_DISCONNECTED);
		break;

	case MAXUSB_EVENT_VBUS:
		pr_info("USB: VBUS (cable connect)\n");
		MXC_USB_EventClear(MAXUSB_EVENT_BRST);
		MXC_USB_EventEnable(MAXUSB_EVENT_BRST, mx_usb_event_cb, NULL);
		MXC_USB_EventClear(MAXUSB_EVENT_BRSTDN);
		MXC_USB_EventEnable(MAXUSB_EVENT_BRSTDN, mx_usb_event_cb, NULL);
		MXC_USB_EventClear(MAXUSB_EVENT_SUSP);
		MXC_USB_EventEnable(MAXUSB_EVENT_SUSP, mx_usb_event_cb, NULL);
		MXC_USB_Connect();
		break;

	case MAXUSB_EVENT_BRST:
		pr_info("USB: bus reset\n");
		enum_clearconfig();
		iio_usb_on_event(IIO_USB_EVENT_RESET);
		break;

	case MAXUSB_EVENT_BRSTDN:
		mx_usb_select_speed();
		break;

	default:
		break;
	}

	return 0;
}

static int mx_usb_setconfig_cb(MXC_USB_SetupPkt *sud, void *cbdata)
{
	unsigned int i;

	if (sud->wValue == desc_cfg_fs[5]) {
		mx_usb_select_speed();

		for (i = 0; i < num_pipes; i++) {
			MXC_USB_ConfigEp(pipe_ep_in(i), MAXUSB_EP_TYPE_IN,
					 usb_mps);
			MXC_USB_ConfigEp(pipe_ep_out(i), MAXUSB_EP_TYPE_OUT,
					 usb_mps);
		}

		iio_usb_on_event(IIO_USB_EVENT_CONNECTED);
		return 0;
	} else if (sud->wValue == 0) {
		iio_usb_on_event(IIO_USB_EVENT_DISCONNECTED);
		return 0;
	}

	return -1;
}

static int mx_usb_setfeature_cb(MXC_USB_SetupPkt *sud, void *cbdata)
{
	if (sud->wValue == FEAT_REMOTE_WAKE)
		return 0;

	return -1;
}

static int mx_usb_clrfeature_cb(MXC_USB_SetupPkt *sud, void *cbdata)
{
	if (sud->wValue == FEAT_REMOTE_WAKE)
		return 0;

	return -1;
}

static int mx_usb_vendor_req_cb(MXC_USB_SetupPkt *sud, void *cbdata)
{
	switch (sud->bRequest) {
	case IIO_USB_CTRL_RESET_PIPES:
		iio_usb_on_ctrl(IIO_USB_CTRL_RESET_PIPES, 0);
		return 0;

	case IIO_USB_CTRL_OPEN_PIPE:
		if (sud->wValue >= num_pipes)
			return -1;
		iio_usb_on_ctrl(IIO_USB_CTRL_OPEN_PIPE, sud->wValue);
		return 0;

	case IIO_USB_CTRL_CLOSE_PIPE:
		if (sud->wValue >= num_pipes)
			return -1;
		iio_usb_on_ctrl(IIO_USB_CTRL_CLOSE_PIPE, sud->wValue);
		return 0;

	default:
		return -1;
	}
}

static void mx_usb_irq_handler(void)
{
	MXC_USB_EventHandler();
}

static void mx_usb_delay_us(unsigned int usec)
{
	no_os_udelay((uint32_t)usec);
}

static int mx_usb_init(const struct iio_usb_dev_info *info)
{
	maxusb_cfg_options_t opts;
	int ret;

	if (!info->num_pipes || info->num_pipes > IIO_USB_MAX_PIPES)
		return -EINVAL;

	num_pipes = info->num_pipes;
	usb_mps = MX_USB_MAX_PACKET_FS;

	desc_build(info);

	opts.enable_hs = 1;
	opts.delay_us = mx_usb_delay_us;
	opts.init_callback = usbStartupCallback;
	opts.shutdown_callback = usbShutdownCallback;

	ret = MXC_USB_Init(&opts);
	if (ret) {
		pr_err("MXC_USB_Init failed: %d\n", ret);
		return -EIO;
	}

	ret = enum_init();
	if (ret) {
		pr_err("enum_init failed: %d\n", ret);
		return -EIO;
	}

	enum_register_descriptor(ENUM_DESC_DEVICE, desc_dev, 0);
	enum_register_descriptor(ENUM_DESC_CONFIG, desc_cfg_fs, 0);
	enum_register_descriptor(ENUM_DESC_OTHER, desc_cfg_hs, 0);
	enum_register_descriptor(ENUM_DESC_QUAL, desc_qual, 0);

	enum_register_descriptor(ENUM_DESC_STRING, str_lang, 0);
	enum_register_descriptor(ENUM_DESC_STRING, str_mfg, 1);
	enum_register_descriptor(ENUM_DESC_STRING, str_prod, 2);
	enum_register_descriptor(ENUM_DESC_STRING, str_serial, 3);
	enum_register_descriptor(ENUM_DESC_STRING, str_iface, 4);

	enum_register_callback(ENUM_SETCONFIG, mx_usb_setconfig_cb, NULL);
	enum_register_callback(ENUM_SETFEATURE, mx_usb_setfeature_cb, NULL);
	enum_register_callback(ENUM_CLRFEATURE, mx_usb_clrfeature_cb, NULL);
	enum_register_callback(ENUM_VENDOR_REQ, mx_usb_vendor_req_cb, NULL);

	MXC_USB_EventEnable(MAXUSB_EVENT_NOVBUS, mx_usb_event_cb, NULL);
	MXC_USB_EventEnable(MAXUSB_EVENT_VBUS, mx_usb_event_cb, NULL);

	MXC_NVIC_SetVector(USB_IRQn, mx_usb_irq_handler);
	NVIC_EnableIRQ(USB_IRQn);

	return 0;
}

static void mx_usb_remove(void)
{
	NVIC_DisableIRQ(USB_IRQn);
	MXC_USB_Disconnect();
	MXC_USB_Shutdown();
}

const struct iio_usb_ops iio_usb_ops = {
	.init		= mx_usb_init,
	.remove		= mx_usb_remove,
	.submit_rx	= mx_usb_submit_rx,
	.submit_tx	= mx_usb_submit_tx,
	.cancel		= mx_usb_cancel,
	.flush		= mx_usb_flush,
	.max_packet	= mx_usb_max_packet,
};
