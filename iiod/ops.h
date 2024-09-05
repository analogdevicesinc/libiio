/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#ifndef __OPS_H__
#define __OPS_H__

#include "../iio-config.h"
#include "queue.h"

#include <errno.h>
#include <iio/iio.h>
#include <iio/iio-backend.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#if WITH_AIO
#include <libaio.h>
#endif

#define IIOD_PORT 30431

#ifndef __bswap_constant_16
#define __bswap_constant_16(x) \
	((unsigned short int) ((((x) >> 8) & 0xff) | (((x) & 0xff) << 8)))
#endif

#ifndef __bswap_constant_32
#define __bswap_constant_32(x) \
	((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) | \
	 (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))
#endif

#define BIT(x) (1 << (x))
#define BIT_MASK(bit) BIT((bit) % 32)
#define BIT_WORD(bit) ((bit) / 32)
#define TEST_BIT(addr, bit) (!!(*(((uint32_t *) addr) + BIT_WORD(bit)) \
		& BIT_MASK(bit)))

struct iio_mutex;
struct iio_task;
struct iiod_io;
struct pollfd;
struct parser_pdata;
struct thread_pool;
extern struct thread_pool *main_thread_pool;
struct DevEntry;

extern struct iio_mutex *buflist_lock;
extern struct iio_mutex *evlist_lock;

struct block_entry {
	SLIST_ENTRY(block_entry) entry;
	struct iio_block *block;
	struct iiod_io *io;
	uint64_t bytes_used;
	uint16_t idx;
	bool cyclic;
	int dmabuf_fd;
	int ep_fd;
};

struct buffer_entry {
	SLIST_ENTRY(buffer_entry) entry;
	struct parser_pdata *pdata;
	const struct iio_device *dev;
	struct iio_buffer *buf;
	struct iio_task *enqueue_task, *dequeue_task;
	uint32_t *words;
	uint16_t idx;
	bool is_tx;

	SLIST_HEAD(BlockList, block_entry) blocklist;
	struct iio_mutex *lock;
};

struct evstream_entry {
	SLIST_ENTRY(evstream_entry) entry;
	struct parser_pdata *pdata;
	const struct iio_device *dev;
	struct iio_event_stream *stream;
	struct iio_task *task;
	struct iiod_io *io;
	uint16_t client_id;
};

struct parser_pdata {
	struct iio_context *ctx;
	bool stop, binary;
	int fd_in, fd_out;
	int resp_fd[2];

	SLIST_HEAD(ParserDataThdHead, ThdEntry) thdlist_head;

	/* Used as temporaries placements by the lexer */
	struct iio_device *dev;
	struct iio_channel *chn;
	bool channel_is_output;
	bool fd_in_is_socket, fd_out_is_socket;
	bool is_usb;
#if WITH_AIO
	io_context_t aio_ctx[2];
	int aio_eventfd[2];
	pthread_mutex_t aio_mutex[2];
#endif
	struct thread_pool *pool;
	struct iiod_io *io;

	const void *xml_zstd;
	size_t xml_zstd_len;

	ssize_t (*writefd)(struct parser_pdata *pdata, const void *buf, size_t len);
	ssize_t (*readfd)(struct parser_pdata *pdata, void *buf, size_t len);
};

struct iio_device_pdata {
	struct DevEntry *entry;
	unsigned int nb_blocks;
};

extern bool server_demux; /* Defined in iiod.c */

void interpreter(struct iio_context *ctx, int fd_in, int fd_out,
		 bool is_socket, bool is_usb, struct thread_pool *pool,
		 const void *xml_zstd, size_t xml_zstd_len);
void ascii_interpreter(struct parser_pdata *pdata);

int init_usb_daemon(const char *ffs, unsigned int nb_pipes);
int start_usb_daemon(struct iio_context *ctx, const char *ffs,
		     unsigned int nb_pipes,
		     int ep0_fd, struct thread_pool *pool,
		     const void *xml_zstd, size_t xml_zstd_len);
int start_serial_daemon(struct iio_context *ctx, const char *uart_params,
			struct thread_pool *pool,
			const void *xml_zstd, size_t xml_zstd_len);
int start_network_daemon(struct iio_context *ctx,
			 struct thread_pool *pool, const void *xml_zstd,
			 size_t xml_zstd_len, uint16_t port);

int usb_attach_dmabuf(int ep_fd, int fd);
int usb_detach_dmabuf(int ep_fd, int fd);
int usb_transfer_dmabuf(int ep_fd, int fd, uint64_t size);

int binary_parse(struct parser_pdata *pdata);

void enable_binary(struct parser_pdata *pdata);

int open_dev(struct parser_pdata *pdata, struct iio_device *dev,
		size_t samples_count, const char *mask, bool cyclic);
int close_dev(struct parser_pdata *pdata, struct iio_device *dev);

ssize_t rw_dev(struct parser_pdata *pdata, struct iio_device *dev,
		unsigned int nb, bool is_write);

ssize_t read_dev_attr(struct parser_pdata *pdata, struct iio_device *dev,
		const char *attr, enum iio_attr_type type);
ssize_t write_dev_attr(struct parser_pdata *pdata, struct iio_device *dev,
		const char *attr, size_t len, enum iio_attr_type type);

ssize_t read_chn_attr(struct parser_pdata *pdata, struct iio_channel *chn,
		const char *attr);
ssize_t write_chn_attr(struct parser_pdata *pdata, struct iio_channel *chn,
		const char *attr, size_t len);

ssize_t get_trigger(struct parser_pdata *pdata, struct iio_device *dev);
ssize_t set_trigger(struct parser_pdata *pdata,
		struct iio_device *dev, const char *trig);

int set_timeout(struct parser_pdata *pdata, unsigned int timeout);
int set_buffers_count(struct parser_pdata *pdata,
		struct iio_device *dev, long value);

ssize_t read_line(struct parser_pdata *pdata, char *buf, size_t len);
ssize_t read_all(struct parser_pdata *pdata, void *dst, size_t len);
ssize_t write_all(struct parser_pdata *pdata, const void *src, size_t len);

static __inline__ void output(struct parser_pdata *pdata, const char *text)
{
	if (write_all(pdata, text, strlen(text)) <= 0)
		pdata->stop = true;
}

int poll_nointr(struct pollfd *pfd, unsigned int num_pfd);

#endif /* __OPS_H__ */
