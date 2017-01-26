/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2016 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include "../debug.h"
#include "../iio-private.h"
#include "ops.h"
#include "thread-pool.h"

#include <fcntl.h>
#include <linux/usb/functionfs.h>
#include <stdint.h>
#include <string.h>

/* u8"IIO" for non-c11 compilers */
#define NAME "\x0049\x0049\x004F"

#define LE32(x) ((__BYTE_ORDER != __BIG_ENDIAN) ? (x) : __bswap_constant_32(x))
#define LE16(x) ((__BYTE_ORDER != __BIG_ENDIAN) ? (x) : __bswap_constant_16(x))

#define IIO_USD_CMD_RESET_PIPES 0
#define IIO_USD_CMD_OPEN_PIPE 1
#define IIO_USD_CMD_CLOSE_PIPE 2


struct usb_ffs_header {
	struct usb_functionfs_descs_head_v2 header;
	uint32_t nb_fs, nb_hs, nb_ss;
} __attribute__((packed));

struct usb_ffs_strings {
	struct usb_functionfs_strings_head head;
	uint16_t lang;
	const char string[sizeof(NAME)];
} __attribute__((packed));

struct usbd_pdata {
	struct iio_context *ctx;
	char *ffs;
	int ep0_fd;
	bool debug, use_aio;
	struct thread_pool **pool;
	unsigned int nb_pipes;
};

struct usbd_client_pdata {
	struct usbd_pdata *pdata;
	int ep_in, ep_out;
};

static const struct usb_ffs_strings ffs_strings = {
	.head = {
		.magic = LE32(FUNCTIONFS_STRINGS_MAGIC),
		.length = LE32(sizeof(ffs_strings)),
		.str_count = LE32(1),
		.lang_count = LE32(1),
	},

	.lang = LE16(0x409),
	.string = NAME,
};

static void usbd_client_thread(struct thread_pool *pool, void *d)
{
	struct usbd_client_pdata *pdata = d;

	interpreter(pdata->pdata->ctx, pdata->ep_in, pdata->ep_out,
			pdata->pdata->debug, false,
			pdata->pdata->use_aio, pool);

	close(pdata->ep_in);
	close(pdata->ep_out);
	free(pdata);
}

static int usb_open_pipe(struct usbd_pdata *pdata, unsigned int pipe_id)
{
	struct usbd_client_pdata *cpdata;
	char buf[256];
	int err;

	if (pipe_id >= pdata->nb_pipes)
		return -EINVAL;

	cpdata = malloc(sizeof(*cpdata));
	if (!pdata)
		return -ENOMEM;

	/* Either we open this pipe for the first time, or it was closed before.
	 * In that case we called thread_pool_stop() without waiting for all the
	 * threads to finish. We do that here. Since the running thread might still
	 * have a open handle to the endpoints make sure that they have exited
	 * before opening the endpoints again. */
	thread_pool_stop_and_wait(pdata->pool[pipe_id]);

	snprintf(buf, sizeof(buf), "%s/ep%u", pdata->ffs, pipe_id * 2 + 1);
	cpdata->ep_out = open(buf, O_WRONLY);
	if (cpdata->ep_out < 0) {
		err = -errno;
		goto err_free_cpdata;
	}

	snprintf(buf, sizeof(buf), "%s/ep%u", pdata->ffs, pipe_id * 2 + 2);
	cpdata->ep_in = open(buf, O_RDONLY);
	if (cpdata->ep_in < 0) {
		err = -errno;
		goto err_close_ep_out;
	}

	cpdata->pdata = pdata;

	err = thread_pool_add_thread(pdata->pool[pipe_id],
			usbd_client_thread, cpdata, "usbd_client_thd");
	if (!err)
		return 0;

	close(cpdata->ep_in);
err_close_ep_out:
	close(cpdata->ep_out);
err_free_cpdata:
	free(cpdata);
	return err;
}

static int usb_close_pipe(struct usbd_pdata *pdata, unsigned int pipe_id)
{
	if (pipe_id >= pdata->nb_pipes)
		return -EINVAL;

	thread_pool_stop(pdata->pool[pipe_id]);
	return 0;
}

static void usb_close_pipes(struct usbd_pdata *pdata)
{
	unsigned int i;

	for (i = 0; i < pdata->nb_pipes; i++)
		usb_close_pipe(pdata, i);
}

static int handle_event(struct usbd_pdata *pdata,
		const struct usb_functionfs_event *event)
{
	int ret = 0;

	if (event->type == FUNCTIONFS_SETUP) {
		const struct usb_ctrlrequest *req = &event->u.setup;

		switch (req->bRequest) {
		case IIO_USD_CMD_RESET_PIPES:
			usb_close_pipes(pdata);
			break;
		case IIO_USD_CMD_OPEN_PIPE:
			ret = usb_open_pipe(pdata, le16toh(req->wValue));
			break;
		case IIO_USD_CMD_CLOSE_PIPE:
			ret = usb_close_pipe(pdata, le16toh(req->wValue));
			break;
		}
	}

	return ret;
}

static void usbd_main(struct thread_pool *pool, void *d)
{
	int stop_fd = thread_pool_get_poll_fd(pool);
	struct usbd_pdata *pdata = d;
	unsigned int i;

	for (;;) {
		struct usb_functionfs_event event;
		struct pollfd pfd[2];
		int ret;

		pfd[0].fd = pdata->ep0_fd;
		pfd[0].events = POLLIN;
		pfd[0].revents = 0;
		pfd[1].fd = stop_fd;
		pfd[1].events = POLLIN;
		pfd[1].revents = 0;

		poll_nointr(pfd, 2);

		if (pfd[1].revents & POLLIN) /* STOP event */
			break;

		if (!(pfd[0].revents & POLLIN)) /* Should never happen. */
			continue;

		ret = read(pdata->ep0_fd, &event, sizeof(event));
		if (ret != sizeof(event)) {
			WARNING("Short read!\n");
			continue;
		}

		ret = handle_event(pdata, &event);
		if (ret) {
			ERROR("Unable to handle event: %i\n", ret);
			break;
		}

		/* Clear out the errors on ep0 when we close endpoints */
		ret = read(pdata->ep0_fd, NULL, 0);
	}

	for (i = 0; i < pdata->nb_pipes; i++) {
		thread_pool_stop_and_wait(pdata->pool[i]);
		thread_pool_destroy(pdata->pool[i]);
	}

	close(pdata->ep0_fd);
	free(pdata->ffs);
	free(pdata->pool);
	free(pdata);
}

static struct usb_ffs_header * create_header(
		unsigned int nb_pipes, uint32_t size)
{
	/* Packet sizes for USB high-speed, full-speed, super-speed */
	const unsigned int packet_sizes[3] = { 64, 512, 512, };
	struct usb_ffs_header *hdr;
	unsigned int i, pipe_id;
	uintptr_t ptr;

	hdr = zalloc(size);
	if (!hdr) {
		errno = ENOMEM;
		return NULL;
	}

	hdr->header.magic = LE32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2);
	hdr->header.length = htole32(size);
	hdr->header.flags = LE32(FUNCTIONFS_HAS_FS_DESC |
			FUNCTIONFS_HAS_HS_DESC |
			FUNCTIONFS_HAS_SS_DESC);

	hdr->nb_fs = htole32(nb_pipes * 2 + 1);
	hdr->nb_hs = htole32(nb_pipes * 2 + 1);
	hdr->nb_ss = htole32(nb_pipes * 2 + 1);

	ptr = ((uintptr_t) hdr) + sizeof(*hdr);

	for (i = 0; i < 3; i++) {
		struct usb_interface_descriptor *desc =
			(struct usb_interface_descriptor *) ptr;
		struct usb_endpoint_descriptor_no_audio *ep;

		desc->bLength = sizeof(*desc);
		desc->bDescriptorType = USB_DT_INTERFACE;
		desc->bNumEndpoints = nb_pipes * 2;
		desc->bInterfaceClass = USB_CLASS_COMM;
		desc->iInterface = 1;

		ep = (struct usb_endpoint_descriptor_no_audio *)
			(ptr + sizeof(*desc));

		for (pipe_id = 0; pipe_id < nb_pipes; pipe_id++) {
			ep->bLength = sizeof(*ep);
			ep->bDescriptorType = USB_DT_ENDPOINT;
			ep->bEndpointAddress = (pipe_id + 1) | USB_DIR_IN;
			ep->bmAttributes = USB_ENDPOINT_XFER_BULK;
			ep->wMaxPacketSize = htole16(packet_sizes[i]);
			ep++;

			ep->bLength = sizeof(*ep);
			ep->bDescriptorType = USB_DT_ENDPOINT;
			ep->bEndpointAddress = (pipe_id + 1) | USB_DIR_OUT;
			ep->bmAttributes = USB_ENDPOINT_XFER_BULK;
			ep->wMaxPacketSize = htole16(packet_sizes[i]);
			ep++;
		}

		ptr += sizeof(*desc) + nb_pipes * 2 * sizeof(*ep);
	}

	return hdr;
}

static int write_header(int fd, unsigned int nb_pipes)
{
	uint32_t size = sizeof(struct usb_ffs_header) +
		3 * sizeof(struct usb_interface_descriptor) +
		6 * nb_pipes * sizeof(struct usb_endpoint_descriptor_no_audio);
	struct usb_ffs_header *hdr;
	int ret;

	hdr = create_header(nb_pipes, size);
	if (!hdr)
		return -errno;

	ret = write(fd, hdr, size);
	free(hdr);
	if (ret < 0)
		return -errno;

	ret = write(fd, &ffs_strings, sizeof(ffs_strings));
	if (ret < 0)
		return -errno;

	return 0;
}

int start_usb_daemon(struct iio_context *ctx, const char *ffs,
		bool debug, bool use_aio, unsigned int nb_pipes,
		struct thread_pool *pool)
{
	struct usbd_pdata *pdata;
	unsigned int i;
	char buf[256];
	int ret;

	pdata = zalloc(sizeof(*pdata));
	if (!pdata)
		return -ENOMEM;

	pdata->nb_pipes = nb_pipes;

	pdata->pool = calloc(nb_pipes, sizeof(*pdata->pool));
	if (!pdata->pool) {
		ret = -ENOMEM;
		goto err_free_pdata;
	}

	pdata->ffs = strdup(ffs);
	if (!pdata->ffs) {
		ret = -ENOMEM;
		goto err_free_pdata_pool;
	}

	snprintf(buf, sizeof(buf), "%s/ep0", ffs);

	pdata->ep0_fd = open(buf, O_RDWR);
	if (pdata->ep0_fd < 0) {
		ret = -errno;
		goto err_free_ffs;
	}

	ret = write_header(pdata->ep0_fd, nb_pipes);
	if (ret < 0)
		goto err_close_ep0;

	for (i = 0; i < nb_pipes; i++) {
		pdata->pool[i] = thread_pool_new();
		if (!pdata->pool[i]) {
			ret = -errno;
			goto err_free_pools;
		}
	}

	pdata->ctx = ctx;
	pdata->debug = debug;
	pdata->use_aio = use_aio;

	ret = thread_pool_add_thread(pool, usbd_main, pdata, "usbd_main_thd");
	if (!ret)
		return 0;

err_free_pools:
	/* If we get here, usbd_main was not started, so the pools can be
	 * destroyed directly */
	for (i = 0; i < nb_pipes; i++) {
		if (pdata->pool[i])
			thread_pool_destroy(pdata->pool[i]);
	}
err_close_ep0:
	close(pdata->ep0_fd);
err_free_ffs:
	free(pdata->ffs);
err_free_pdata_pool:
	free(pdata->pool);
err_free_pdata:
	free(pdata);
	return ret;
}
