// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * iiod's UDP sample stream provider. For each buffer with a stream, a thread
 * dequeues blocks from the kernel, sends each as a run of datagrams (the
 * 16-byte header of iiod-udp-stream.h, then payload_bytes of samples straight
 * from the block) and gives the block back. Nothing paces it: it sends as fast
 * as the device fills blocks and the socket takes datagrams. When the socket
 * is slower, the device overruns and samples are lost between blocks, which
 * the datagram sequence does not show.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <iio/iio-debug.h>
#include <iio/iio-lock.h>
#include <iio/iio.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "debug.h"
#include "ops.h"
#include "udp-provider.h"
#include "udp-stream.h"

/* Datagrams per sendmmsg() */
#define BATCH 64
/* Kernel blocks per stream */
#define NB_BLOCKS 4
/* payload_bytes is a multiple of this, as with the other servers */
#define PAYLOAD_ALIGN 16
/* The largest payload served, whatever the client accepts: what fits in an
 * IPv4 datagram after the header, rounded down */
#define MAX_PAYLOAD_BYTES 65488
/* The socket's send buffer. Small on purpose: a blocked sender, not a full
 * transmit queue dropping datagrams, is what holds the stream back. */
#define SNDBUF_BYTES (1024 * 1024)
/* The longest the sender waits before it looks at its stop flag again */
#define SEND_TIMEOUT_US 100000
#define DEQUEUE_NAP_NS 100000
/* How long stop() waits for the blocks the kernel still holds */
#define DRAIN_TIMEOUT_MS 500

struct udp_provider {
	struct iio_mutex *lock; /* ids */
	uint64_t ids;
	uint16_t port;
	size_t block_size;
};

/* One buffer's stream, in entry->udp_priv */
struct udp_buf {
	int fd;
	uint16_t payload_bytes;
	uint32_t stream_id;
	bool armed;

	/* The kernel blocks, enqueued and dequeued in order from <head> */
	struct iio_block *blocks[NB_BLOCKS];
	bool queued[NB_BLOCKS];
	unsigned int head;
	size_t block_size;

	struct iio_thrd *thrd;
	struct iio_mutex *lock; /* stop */
	bool stop;

	uint64_t seq;
	uint8_t hdrs[BATCH][IIOD_UDP_STREAM_HDR_LEN];
	struct iovec iov[BATCH][2];
	struct mmsghdr msgs[BATCH];
};

static struct udp_provider provider;

static uint64_t mix64(uint64_t x)
{
	x += 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;

	return x ^ (x >> 31);
}

/* Never 0, never the buffer's previous one */
static uint32_t udp_new_id(struct udp_provider *udp, uint32_t prev)
{
	uint32_t id;

	iio_mutex_lock(udp->lock);
	do {
		id = (uint32_t)(mix64(++udp->ids) >> 32);
	} while (!id || id == prev);
	iio_mutex_unlock(udp->lock);

	return id;
}

static size_t gcd(size_t a, size_t b)
{
	size_t t;

	while (b) {
		t = a % b;
		a = b;
		b = t;
	}

	return a;
}

static void udp_free_blocks(struct udp_buf *ub)
{
	unsigned int i;

	for (i = 0; i < NB_BLOCKS; i++) {
		if (ub->blocks[i])
			iio_block_destroy(ub->blocks[i]);
		ub->blocks[i] = NULL;
		ub->queued[i] = false;
	}

	ub->head = 0;
	ub->block_size = 0;
}

static int udp_create_blocks(struct udp_buf *ub, struct buffer_entry *entry, size_t size)
{
	unsigned int i;
	int ret;

	for (i = 0; i < NB_BLOCKS; i++) {
		ub->blocks[i] = iio_buffer_stream_create_block(entry->buf_stream, size);
		ret = iio_err(ub->blocks[i]);
		if (ret) {
			ub->blocks[i] = NULL;
			udp_free_blocks(ub);
			return ret;
		}
	}

	ub->block_size = size;

	return 0;
}

/* Give the kernel every block it does not hold, oldest first */
static int udp_enqueue_blocks(struct udp_buf *ub)
{
	unsigned int i, k;
	int ret;

	for (k = 0; k < NB_BLOCKS; k++) {
		i = (ub->head + k) % NB_BLOCKS;
		if (ub->queued[i])
			continue;

		ret = iio_block_enqueue(ub->blocks[i], ub->block_size, false);
		if (ret)
			return ret;

		ub->queued[i] = true;
	}

	return 0;
}

/* A socket bound to the provider's port and connected to the client, so that
 * the kernel picks the source address the client is told */
static int udp_open_socket(const struct udp_provider *udp, const struct iiod_udp_stream_req *req,
		struct sockaddr_in *src)
{
	struct sockaddr_in addr = { .sin_family = AF_INET };
	struct timeval tv = { .tv_usec = SEND_TIMEOUT_US };
	socklen_t len = sizeof(*src);
	int fd, err, val = 1;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return -errno;

	/* One socket per stream, all from the same port */
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
	val = SNDBUF_BYTES;
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &val, sizeof(val));

	addr.sin_port = htons(udp->port);
	if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0 ||
			bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		goto err_close;

	addr.sin_port = htons(req->dst_port);
	addr.sin_addr.s_addr = htonl(req->dst_ip);
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
			getsockname(fd, (struct sockaddr *)src, &len) < 0)
		goto err_close;

	return fd;

err_close:
	err = -errno;
	close(fd);
	return err;
}

static int udp_setup(void *priv, struct buffer_entry *entry, const struct iiod_udp_stream_req *req,
		struct iiod_udp_stream_resp *resp)
{
	struct udp_provider *udp = priv;
	struct udp_buf *ub = entry->udp_priv;
	const struct iio_channels_mask *mask;
	size_t step, limit, payload, block_size;
	struct sockaddr_in src;
	ssize_t sample_size;
	unsigned int i;
	int ret, fd;

	if (!entry->buf_stream)
		return -EINVAL;

	/* The samples come from the device: an output buffer has none to send */
	if (entry->is_tx)
		return -ENOTSUP;

	/* The responder answers that itself; better safe */
	if (ub && ub->thrd)
		return -EBUSY;

	mask = iio_buffer_stream_get_channels_mask(entry->buf_stream);
	sample_size = iio_device_get_sample_size(entry->dev, mask);
	if (sample_size <= 0)
		return -ENOTSUP;

	/* The largest multiple of 16 and of the sample-set size that fits */
	step = PAYLOAD_ALIGN / gcd(PAYLOAD_ALIGN, (size_t)sample_size) * (size_t)sample_size;
	limit = req->max_payload < MAX_PAYLOAD_BYTES ? req->max_payload : MAX_PAYLOAD_BYTES;
	payload = limit / step * step;
	if (payload < PAYLOAD_ALIGN)
		return -EINVAL;

	if (!req->dst_ip || !req->dst_port)
		return -EINVAL;

	/* A whole number of datagrams per block, one at least */
	block_size = udp->block_size / payload * payload;
	if (!block_size)
		block_size = payload;

	fd = udp_open_socket(udp, req, &src);
	if (fd < 0) {
		IIO_PERROR(fd, "UDP stream: unable to open a socket from port %u", udp->port);
		return fd;
	}

	if (!ub) {
		ub = calloc(1, sizeof(*ub));
		if (!ub) {
			ret = -ENOMEM;
			goto err_close;
		}

		ub->lock = iio_mutex_create();
		ret = iio_err(ub->lock);
		if (ret) {
			free(ub);
			goto err_close;
		}

		ub->fd = -1;
		entry->udp_priv = ub;
	}

	/* The blocks are created here, before the enable, and live as long as
	 * the buffer. A new payload size needs new ones, which the kernel only
	 * gives once it holds none of the old. */
	if (ub->block_size && ub->block_size != block_size) {
		for (i = 0; i < NB_BLOCKS; i++) {
			if (ub->queued[i]) {
				ret = -EBUSY;
				goto err_close;
			}
		}

		udp_free_blocks(ub);
	}

	if (!ub->block_size) {
		ret = udp_create_blocks(ub, entry, block_size);
		if (ret) {
			IIO_PERROR(ret, "UDP stream: unable to create %u blocks of %zu bytes",
					NB_BLOCKS, block_size);
			goto err_close;
		}
	}

	/* Queued before the enable, as a TCP client's blocks are: the capture
	 * starts with the device, not some time after it */
	ret = udp_enqueue_blocks(ub);
	if (ret) {
		IIO_PERROR(ret, "UDP stream: unable to enqueue a block");
		goto err_close;
	}

	if (ub->fd >= 0)
		close(ub->fd);
	ub->fd = fd;
	ub->payload_bytes = (uint16_t)payload;
	ub->stream_id = udp_new_id(udp, ub->stream_id);
	ub->armed = true;

	resp->stream_id = ub->stream_id;
	resp->payload_bytes = ub->payload_bytes;
	resp->src_ip = ntohl(src.sin_addr.s_addr);
	resp->src_port = ntohs(src.sin_port);

	IIO_INFO("UDP stream %08" PRIx32 ": %s buffer %u, port %u to %u.%u.%u.%u:%u, "
		 "%u-byte payloads, %zu-byte blocks\n",
			ub->stream_id, iio_device_get_id(entry->dev), entry->idx, resp->src_port,
			req->dst_ip >> 24, (req->dst_ip >> 16) & 0xff, (req->dst_ip >> 8) & 0xff,
			req->dst_ip & 0xff, req->dst_port, ub->payload_bytes, ub->block_size);

	return 0;

err_close:
	close(fd);
	return ret;
}

static bool udp_stopping(struct udp_buf *ub)
{
	bool stop;

	iio_mutex_lock(ub->lock);
	stop = ub->stop;
	iio_mutex_unlock(ub->lock);

	return stop;
}

/* Send <nb> datagrams whose payloads follow each other from <data>. A full
 * socket is waited for, unless the stream is being stopped. */
static int udp_send(struct udp_buf *ub, const uint8_t *data, unsigned int nb)
{
	struct iiod_udp_stream_hdr hdr = {
		.version = IIOD_UDP_STREAM_VERSION,
		.payload_length = ub->payload_bytes,
		.stream_id = ub->stream_id,
	};
	unsigned int i, done = 0;
	int ret;

	for (i = 0; i < nb; i++) {
		hdr.sequence = ub->seq + i;
		iiod_udp_stream_hdr_pack(&hdr, ub->hdrs[i]);
		ub->iov[i][1].iov_base = (void *)(data + (size_t)i * ub->payload_bytes);
	}

	while (done < nb) {
		ret = sendmmsg(ub->fd, ub->msgs + done, nb - done, 0);
		if (ret > 0) {
			done += (unsigned int)ret;
			continue;
		}

		if (ret < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK &&
				errno != ENOBUFS)
			return -errno;

		/* What was not sent is not numbered either */
		if (udp_stopping(ub))
			break;
	}

	ub->seq += done;

	return 0;
}

static int udp_sender(void *d)
{
	struct udp_buf *ub = d;
	const struct timespec nap = { .tv_nsec = DEQUEUE_NAP_NS };
	size_t per_block = ub->block_size / ub->payload_bytes;
	const uint8_t *data;
	unsigned int i, nb;
	size_t done;
	int err = 0;

	while (!err && !udp_stopping(ub)) {
		i = ub->head;

		err = iio_block_dequeue(ub->blocks[i], true);
		if (err == -EBUSY || err == -EAGAIN || err == -ETIMEDOUT) {
			err = 0;
			nanosleep(&nap, NULL);
			continue;
		}
		if (err)
			break;

		ub->queued[i] = false;
		data = iio_block_start(ub->blocks[i]);

		for (done = 0; !err && done < per_block && !udp_stopping(ub); done += nb) {
			nb = per_block - done < BATCH ? (unsigned int)(per_block - done) : BATCH;
			err = udp_send(ub, data + done * ub->payload_bytes, nb);
		}
		if (err)
			break;

		err = iio_block_enqueue(ub->blocks[i], ub->block_size, false);
		if (err)
			break;

		ub->queued[i] = true;
		ub->head = (i + 1) % NB_BLOCKS;
	}

	if (err)
		IIO_PERROR(err, "UDP stream %08" PRIx32 " ended", ub->stream_id);

	return err;
}

/*
 * Take back the blocks the kernel still holds, so that the capture is idle
 * when the buffer is disabled next. Some DMA pipelines do not recover from a
 * disable in the middle of a transfer: the data offload of ADI's reference
 * designs is left waiting for a read-out that never ends, and the device
 * delivers no sample until it is reset. A device too slow to fill them in
 * DRAIN_TIMEOUT_MS keeps its blocks.
 */
static void udp_drain_blocks(struct udp_buf *ub)
{
	const struct timespec nap = { .tv_nsec = DEQUEUE_NAP_NS };
	unsigned int i, k, left = DRAIN_TIMEOUT_MS * (1000000 / DEQUEUE_NAP_NS);
	int ret;

	for (k = 0; k < NB_BLOCKS; k++) {
		i = ub->head;
		if (!ub->queued[i])
			break;

		do {
			ret = iio_block_dequeue(ub->blocks[i], true);
			if (ret != -EBUSY && ret != -EAGAIN && ret != -ETIMEDOUT)
				break;
			nanosleep(&nap, NULL);
		} while (--left);

		if (ret)
			break;

		ub->queued[i] = false;
		ub->head = (i + 1) % NB_BLOCKS;
	}
}

static int udp_start(void *priv, struct buffer_entry *entry)
{
	struct udp_buf *ub = entry->udp_priv;
	unsigned int i;
	int ret;

	/* The responder only starts a stream after a successful setup */
	if (!ub || !ub->armed)
		return -EINVAL;
	if (ub->thrd)
		return -EBUSY;

	/* Used up, whatever happens next */
	ub->armed = false;

	ret = udp_enqueue_blocks(ub);
	if (ret) {
		IIO_PERROR(ret, "UDP stream %08" PRIx32 ": unable to enqueue a block",
				ub->stream_id);
		return ret;
	}

	memset(ub->msgs, 0, sizeof(ub->msgs));
	for (i = 0; i < BATCH; i++) {
		ub->iov[i][0].iov_base = ub->hdrs[i];
		ub->iov[i][0].iov_len = IIOD_UDP_STREAM_HDR_LEN;
		ub->iov[i][1].iov_len = ub->payload_bytes;
		ub->msgs[i].msg_hdr.msg_iov = ub->iov[i];
		ub->msgs[i].msg_hdr.msg_iovlen = 2;
	}

	ub->seq = 0;
	ub->stop = false;

	ub->thrd = iio_thrd_create(udp_sender, ub, "iiod-udp");
	ret = iio_err(ub->thrd);
	if (ret) {
		ub->thrd = NULL;
		return ret;
	}

	IIO_INFO("UDP stream %08" PRIx32 " started\n", ub->stream_id);

	return 0;
}

static void udp_stop(void *priv, struct buffer_entry *entry)
{
	struct udp_buf *ub = entry->udp_priv;

	if (!ub)
		return;

	/* A pending setup goes with the stream */
	ub->armed = false;

	if (!ub->thrd)
		return;

	iio_mutex_lock(ub->lock);
	ub->stop = true;
	iio_mutex_unlock(ub->lock);

	iio_thrd_join_and_destroy(ub->thrd);
	ub->thrd = NULL;

	udp_drain_blocks(ub);

	IIO_INFO("UDP stream %08" PRIx32 " stopped: %" PRIu64 " datagrams sent\n", ub->stream_id,
			ub->seq);
}

static void udp_release(void *priv, struct buffer_entry *entry)
{
	struct udp_buf *ub = entry->udp_priv;

	if (!ub)
		return;

	/* The responder has stopped it already */
	udp_stop(priv, entry);

	udp_free_blocks(ub);
	if (ub->fd >= 0)
		close(ub->fd);
	iio_mutex_destroy(ub->lock);
	free(ub);

	entry->udp_priv = NULL;
}

static const struct iiod_udp_stream_ops udp_provider_ops = {
	.setup = udp_setup,
	.start = udp_start,
	.stop = udp_stop,
	.release = udp_release,
};

int iiod_udp_provider_init(uint16_t port, size_t block_size)
{
	struct timespec ts;

	if (!port || !block_size)
		return -EINVAL;

	provider.lock = iio_mutex_create();
	if (iio_err(provider.lock))
		return iio_err(provider.lock);

	clock_gettime(CLOCK_REALTIME, &ts);
	provider.ids = mix64(((uint64_t)getpid() << 32) ^ (uint64_t)ts.tv_sec ^
			     ((uint64_t)ts.tv_nsec << 20));
	provider.port = port;
	provider.block_size = block_size;

	iiod_udp_stream_set_ops(&udp_provider_ops, &provider);

	return 0;
}

void iiod_udp_provider_exit(void)
{
	iiod_udp_stream_set_ops(NULL, NULL);
	iio_mutex_destroy(provider.lock);
	provider.lock = NULL;
}
