/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * Shared between the C side of the DAQIRI backend module and the C++ shim in
 * daqiri-engine.cpp, which is the only file that includes DAQIRI headers.
 */

#ifndef __DAQIRI_PRIVATE_H__
#define __DAQIRI_PRIVATE_H__

#include <iio/iio-backend.h>
#include <iio/iiod-client.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "daqiri-asm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Longest host part accepted in a URI: an RFC 1035 name, or a bracketed IPv6
 * literal with scope id, plus ":65535". */
#define DQ_HOST_MAX (255 + sizeof("[]:65535"))

/* data=: UDP when the server advertises it and a YAML is set; UDP or fail;
 * never UDP */
enum dq_data { DQ_DATA_AUTO = 0, DQ_DATA_UDP = 1, DQ_DATA_TCP = 2 };
enum dq_mem { DQ_MEM_HOST = 0, DQ_MEM_PINNED = 1, DQ_MEM_DEVICE = 2 };

struct dq_opts {
	char yaml[4096];
	char iface[64];
	int rxq;
	enum dq_data data;
	enum dq_mem mem;
	int gpu;
	bool lossy;
	uint32_t udp_dst; /* host order; 0: not given */
	uint16_t data_port;
	unsigned poll_us;
	/* Receive buffer of a socket engine queue's socket, in bytes: -1 sizes
	 * it from the YAML, 0 leaves the kernel's default */
	int rcvbuf;
};

/* <list> is the text after the first ',' of the URI, or NULL. Applies the
 * defaults first, then every key=value pair; -EINVAL on anything unknown. */
int dq_opts_parse(struct dq_opts *o, const char *list, const struct iio_context_params *p);

/* One TCP connection to iiod. A pointer to it is what this module hands to
 * iiod_client_new() as its struct iiod_client_pdata. */
struct dq_mgmt {
	int fd;
	int cancel_fd;
	const struct iio_context_params *params;
};

int dq_mgmt_connect(struct dq_mgmt *m, const char *host, const char *port, int timeout_ms);
void dq_mgmt_close(struct dq_mgmt *m);
extern const struct iiod_client_ops dq_mgmt_ops;

struct dq_engine;
struct dq_rxq;
struct dq_udp_buffer;
struct dq_udp_block;

struct iio_context_pdata {
	struct dq_mgmt mgmt;
	struct iiod_client *client;
	struct dq_opts opts;
	/* Resolved host and management port; buffers connect here */
	char host[DQ_HOST_MAX];
	char port[sizeof("65535")];
	/* What the YAML selects ("socket", "dpdk", "ibverbs", ...), "none"
	 * without a YAML */
	const char *engine_kind;
	/* The server advertises iiod,udp-stream on a binary connection: only
	 * then may it get the UDP setup command */
	bool udp_advertised;

	/* Protects engine and bufs */
	pthread_mutex_t lock;
	/* Acquired by the first UDP buffer open, released at shutdown */
	struct dq_engine *engine;
	/* Open buffers, for their daqiri_* attributes; shutdown closes the UDP
	 * ones the app left open */
	struct iio_buffer_pdata *bufs;
};

enum dq_path { DQ_PATH_TCP = 0, DQ_PATH_UDP = 1 };

struct iio_buffer_pdata {
	struct iio_context_pdata *ctx;
	const struct iio_device *dev;
	unsigned int idx;
	enum dq_path path;

	/* Both paths: a TCP connection to iiod of its own, as ip: does, and
	 * the buffer opened on it */
	struct {
		struct dq_mgmt io;
		struct iiod_client *client;
		struct iiod_client_buffer_pdata *pdata;
	} iiod;

	/* UDP: one engine RX queue, its poller and the stream's control */
	struct dq_rxq *rxq;
	struct dq_udp_buffer *udp;
	struct iio_buffer_pdata *next; /* in ctx->bufs */
};

/*
 * iiod_block is what iiod_client_create_block() returned: iiod-client's own
 * struct iio_block_pdata, a different type behind the same tag. It is only
 * ever handed back to iiod-client, never dereferenced here.
 */
struct iio_block_pdata {
	struct iio_buffer_pdata *buf;
	struct iio_block_pdata *iiod_block;
	struct dq_udp_block *udp;
};

/* daqiri.c: the buffer's own iiod connection and OPEN_BUFFER on it, as ip:
 * does; and the reverse (CLOSE_BUFFER goes on the management connection).
 * Open logs its errors; close takes what open left, after a failed DISABLE
 * no buffer. */
int dq_iiod_open_buffer(struct iio_buffer_pdata *buf, struct iio_channels_mask *mask);
void dq_iiod_close_buffer(struct iio_buffer_pdata *buf);

const char *dq_mem_name(enum dq_mem mem);

/* C++ side (daqiri-engine.cpp). Never throw; errors are negative errnos. */
int dq_yaml_validate(const char *path, char *err, size_t errlen);
const char *dq_engine_version(void);
const char *dq_yaml_engine_kind(const char *path);

/*
 * Engine broker. One DAQIRI engine per process, initialised from the first
 * YAML acquired and never shut down before exit; acquiring another YAML
 * (compared by realpath) is -EBUSY. The first init pins this module in
 * memory so that unloading it cannot pull the code from under DAQIRI's
 * threads, and registers an exit handler after which no RX queue calls
 * into DAQIRI (-ESHUTDOWN). Release only drops the reference.
 */
int dq_engine_acquire(const char *yaml, struct dq_engine **out, char *err, size_t errlen);
void dq_engine_release(struct dq_engine *e);
/* <iface> matches the name or the address (PCI BDF on the raw engines,
 * socket address) of an interface of the YAML; empty: the first */
int dq_engine_get_port(struct dq_engine *e, const char *iface, int *port_id);

/*
 * Where the UDP sample mode datagrams for (port, queue) go to and come from,
 * host order, 0 where the YAML does not say. Socket engine: local_addr, which
 * the engine binds, and remote_addr, which it connects to. Raw engines: the
 * UDP destination port of the flow that steers to the queue and the IPv4
 * address of the kernel's network interface on that PCI device (else the
 * flow's IPv4 destination); the source is what the flow matches, if anything.
 * -EINVAL for no such queue, -ENOTSUP when the engine does not carry UDP.
 */
int dq_engine_udp_endpoint(struct dq_engine *e, int port_id, int queue_id, uint32_t *local_ip,
		uint16_t *local_port, uint32_t *remote_ip, uint16_t *remote_port);

/* The same for interface <iface> of <yaml> (matched as dq_engine_get_port()
 * does) from the parsed file alone: the engine is never started, so it works
 * for any engine this DAQIRI build can parse. For tests and diagnostics. */
int dq_yaml_udp_endpoint(const char *yaml, const char *iface, int queue_id, uint32_t *local_ip,
		uint16_t *local_port, uint32_t *remote_ip, uint16_t *remote_port);

/* What one packet of a receive queue can be; 0 where unknown */
struct dq_rxq_limits {
	size_t buf_size;  /* bytes of one packet buffer, both regions with header-data split */
	size_t num_bufs;  /* packet buffers of the queue (of its smallest region) */
	unsigned int mtu; /* IP MTU of the network interface (raw: of the PCI device) */
	unsigned int max_udp_payload; /* socket engine: max_payload_size */
	bool raw;		      /* a buffer holds the whole Ethernet frame */
};

/* -EINVAL for no such queue, -ENOTSUP when the engine does not carry UDP */
int dq_engine_rx_limits(struct dq_engine *e, int port_id, int queue_id, struct dq_rxq_limits *out);

/* One owner per (port, queue): a second open is -EBUSY. Socket engine UDP
 * queues, and raw queues of the dpdk and ibverbs engines; a raw queue with
 * two memory regions is header-data split. -ENOTSUP for anything else
 * (RoCE, TCP, RDMA, a raw queue with more than two regions). */
int dq_rxq_open(struct dq_engine *e, int port_id, int queue_id, struct dq_rxq **out);
void dq_rxq_close(struct dq_rxq *q);

/*
 * The receive buffer of a socket engine UDP queue's socket, <bytes> asked
 * for, through DAQIRI: socket_get_server_conn_id() for the YAML's local_addr,
 * then socket_setsockopt() with SO_RCVBUFFORCE when <force> (which needs
 * CAP_NET_ADMIN), else SO_RCVBUF (which the kernel caps at
 * net.core.rmem_max). DAQIRI keys its server sockets by address alone, so
 * the connection's (port, queue) (socket_get_port_queue()) must be this
 * queue's. -ENOTSUP for a queue with no socket of its own (raw engines),
 * -ENOENT when DAQIRI has no server socket for local_addr, -EADDRINUSE when
 * the one it has belongs to another interface on the same local_addr, -EIO
 * when setsockopt() failed (DAQIRI logs why), -ESHUTDOWN once the process
 * exits.
 */
int dq_rxq_set_rcvbuf(struct dq_rxq *q, int bytes, bool force);

/*
 * What the kernel made of it (getsockopt(SO_RCVBUF): twice what was granted).
 * DAQIRI has no getsockopt, so the socket is found among this process's file
 * descriptors, each checked and read through a duplicate of its own: the one
 * IPv4 UDP socket bound to local_addr (and connected to remote_addr when the
 * YAML has one). -ENOENT when none or more than one is, -ENOTSUP as above.
 */
int dq_rxq_get_rcvbuf(struct dq_rxq *q, int *bytes);

struct dq_pkt {
	const uint8_t *data; /* segment 0: whole packet (single segment) or headers (HDS) */
	uint32_t len;
	const uint8_t *payload; /* segment 1 when HDS, else NULL */
	uint32_t payload_len;
};

/* One burst popped from a queue. raw: the packets start at the Ethernet
 * header (dpdk, ibverbs), else at the datagram header (socket). */
struct dq_burst {
	struct dq_rxq *q;
	void *handle;
	unsigned nb_pkts;
	bool raw;
};

/* 0 with a burst to free, -EAGAIN when the queue is empty, -ESHUTDOWN once
 * the process exits (stop polling), or -errno */
int dq_rxq_poll(struct dq_rxq *q, struct dq_burst *b);
/* Fills out[] with the first min(nb_pkts, max) packets; returns the count
 * (0 once the process exits) */
unsigned dq_burst_pkts(const struct dq_burst *b, struct dq_pkt *out, unsigned max);
/* Does nothing once the process exits */
void dq_burst_free(struct dq_burst *b);

/* host: 2 MB hugetlb pages from size 2 MB on when the pool has them,
 * otherwise page aligned, THP advised; pinned: cudaHostAlloc (-ENOTSUP
 * without a usable CUDA driver); device: -ENOTSUP in this phase. *mapped is
 * the length of a hugetlb mapping, 0 for anything else: hand it back to
 * dq_mem_free(). */
int dq_mem_alloc(enum dq_mem k, int gpu, size_t size, void **ptr, size_t *mapped);
void dq_mem_free(enum dq_mem k, void *ptr, size_t mapped);

/*
 * UDP sample mode (daqiri-udp.c). The daqiri_* attributes of every input
 * buffer: a UDP buffer answers them itself, the others with
 * dq_attr_read_default().
 */
extern const char *const dq_attr_names[];
extern const unsigned int dq_nb_attrs;
int dq_attr_index(const char *name); /* -ENOENT if it is not one of them */
ssize_t dq_attr_read_default(const struct iio_context_pdata *pdata, enum dq_path path,
		const char *name, char *dst, size_t len);

/*
 * Open a buffer that the decision table sent to UDP: its engine queue, its
 * iiod connection with OPEN_BUFFER, and the probing setup. Returns 0 with
 * buf->path DQ_PATH_UDP, or DQ_PATH_TCP when the server refused the setup and
 * data=auto (the buffer then streams over TCP on that connection), or a
 * negative error (-ENOTSUP for a refusal with data=udp).
 */
int dq_udp_open_buffer(struct iio_buffer_pdata *buf, struct iio_channels_mask *mask);
void dq_udp_close_buffer(struct iio_buffer_pdata *buf);
int dq_udp_enable_buffer(struct iio_buffer_pdata *buf, size_t nb_samples, bool enable);
void dq_udp_cancel_buffer(struct iio_buffer_pdata *buf);

struct dq_udp_block *dq_udp_block_new(struct dq_udp_buffer *v, size_t size, void **data);
void dq_udp_block_free(struct dq_udp_buffer *v, struct dq_udp_block *b);
int dq_udp_enqueue(struct dq_udp_buffer *v, struct dq_udp_block *b, size_t bytes_used);
int dq_udp_dequeue(struct dq_udp_buffer *v, struct dq_udp_block *b, bool nonblock, int timeout_ms);
ssize_t dq_udp_read_attr(struct dq_udp_buffer *v, const char *name, char *dst, size_t len);
int dq_udp_write_attr(struct dq_udp_buffer *v, const char *name, const char *src, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* __DAQIRI_PRIVATE_H__ */
