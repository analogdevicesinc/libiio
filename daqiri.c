// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * DAQIRI backend module: daqiri:<host>[:port][,key=value]*
 * The management plane is a plain TCP iiod connection, like ip:. Buffers
 * stream either in UDP sample mode, as datagrams that the DAQIRI engine
 * receives (daqiri-udp.c), or through iiod as well, like ip: (TCP). The URI
 * key data= and what the server advertises decide, when the buffer is
 * opened.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <iio/iio-backend.h>
#include <iio/iio-debug.h>
#include <iio/iiod-client.h>
#include <limits.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "daqiri-private.h"
#include "iio-config.h"
#include "iio-private.h"

#define DQ_DEFAULT_PORT "30431"
#define DQ_TIMEOUT_MS 5000

extern const struct iio_backend iio_daqiri_backend;

static const char *const dq_data_names[] = {
	[DQ_DATA_AUTO] = "auto",
	[DQ_DATA_UDP] = "udp",
	[DQ_DATA_TCP] = "tcp",
};

static const char *const dq_mem_names[] = {
	[DQ_MEM_HOST] = "host",
	[DQ_MEM_PINNED] = "pinned",
	[DQ_MEM_DEVICE] = "device",
};

const char *dq_mem_name(enum dq_mem mem)
{
	return dq_mem_names[mem];
}

static int dq_parse_name(const char *val, const char *const *names, unsigned int nb)
{
	unsigned int i;

	for (i = 0; i < nb; i++)
		if (!strcmp(val, names[i]))
			return (int)i;

	return -EINVAL;
}

static int dq_parse_ulong(const char *val, unsigned long max, unsigned long *out)
{
	unsigned long tmp;
	char *end;

	if (!val[0] || val[0] == '-')
		return -EINVAL;

	errno = 0;
	tmp = strtoul(val, &end, 0);
	if (*end || errno == ERANGE || tmp > max)
		return -EINVAL;

	*out = tmp;

	return 0;
}

static int dq_parse_int(const char *val, int *out)
{
	unsigned long tmp;
	int ret = dq_parse_ulong(val, INT_MAX, &tmp);

	if (!ret)
		*out = (int)tmp;
	return ret;
}

static int dq_parse_bool(const char *val, bool *out)
{
	unsigned long tmp;
	int ret = dq_parse_ulong(val, 1, &tmp);

	if (!ret)
		*out = tmp;
	return ret;
}

/* A dotted quad, not 0.0.0.0; host order */
static int dq_parse_ipv4(const char *val, uint32_t *out)
{
	struct in_addr addr;

	if (inet_pton(AF_INET, val, &addr) != 1 || !addr.s_addr)
		return -EINVAL;

	*out = ntohl(addr.s_addr);

	return 0;
}

static int dq_copy_str(char *dst, size_t size, const char *val)
{
	size_t len = strlen(val);

	if (len >= size)
		return -EINVAL;

	memcpy(dst, val, len + 1);

	return 0;
}

static int dq_opts_set(struct dq_opts *o, const char *key, const char *val)
{
	unsigned long num;
	int ret;

	if (!strcmp(key, "yaml"))
		return dq_copy_str(o->yaml, sizeof(o->yaml), val);
	if (!strcmp(key, "iface"))
		return dq_copy_str(o->iface, sizeof(o->iface), val);

	if (!strcmp(key, "data")) {
		ret = dq_parse_name(val, dq_data_names, IIO_ARRAY_SIZE(dq_data_names));
		if (ret < 0)
			return ret;
		o->data = (enum dq_data)ret;
		return 0;
	}
	if (!strcmp(key, "udp_dst"))
		return dq_parse_ipv4(val, &o->udp_dst);
	if (!strcmp(key, "mem")) {
		ret = dq_parse_name(val, dq_mem_names, IIO_ARRAY_SIZE(dq_mem_names));
		if (ret < 0)
			return ret;
		o->mem = (enum dq_mem)ret;
		return 0;
	}

	if (!strcmp(key, "rxq"))
		return dq_parse_int(val, &o->rxq);
	if (!strcmp(key, "gpu"))
		return dq_parse_int(val, &o->gpu);
	if (!strcmp(key, "lossy"))
		return dq_parse_bool(val, &o->lossy);

	if (!strcmp(key, "data_port")) {
		ret = dq_parse_ulong(val, UINT16_MAX, &num);
		if (!ret)
			o->data_port = (uint16_t)num;
		return ret;
	}
	if (!strcmp(key, "poll")) {
		ret = dq_parse_ulong(val, UINT_MAX, &num);
		if (!ret)
			o->poll_us = (unsigned)num;
		return ret;
	}
	if (!strcmp(key, "rcvbuf"))
		return dq_parse_int(val, &o->rcvbuf);

	return -EINVAL;
}

int dq_opts_parse(struct dq_opts *o, const char *list, const struct iio_context_params *p)
{
	const char *env = getenv("IIO_DAQIRI_YAML");
	char *dup, *tok, *save, *val;
	int ret = 0;

	memset(o, 0, sizeof(*o));
	o->data = DQ_DATA_AUTO;
	o->mem = DQ_MEM_HOST;
	/* 200 us: with ppoll() the idle wait at 50 us costs ~13% of a core, and
	 * 200 us keeps the worst-case idle latency well below the engine's
	 * 64-packet queue at 1G rates */
	o->poll_us = 200;
	o->rcvbuf = -1;

	if (env && dq_copy_str(o->yaml, sizeof(o->yaml), env)) {
		prm_err(p, "daqiri: IIO_DAQIRI_YAML is longer than %zu bytes\n",
				sizeof(o->yaml) - 1);
		return -EINVAL;
	}

	if (!list || !list[0])
		return 0;

	dup = iio_strdup(list);
	if (!dup)
		return -ENOMEM;

	for (tok = strtok_r(dup, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
		val = strchr(tok, '=');
		if (!val || val == tok) {
			prm_err(p, "daqiri: option \'%s\' is not key=value\n", tok);
			ret = -EINVAL;
			break;
		}

		*val++ = '\0';

		ret = dq_opts_set(o, tok, val);
		if (ret) {
			prm_err(p, "daqiri: invalid option %s=\'%s\'\n", tok, val);
			break;
		}
	}

	free(dup);

	return ret;
}

/* Host/port split copied from network.c, without the DNS-SD fallback. */
static int dq_parse_host(char *host_buf, char **host, char *port, size_t port_len)
{
	char *first_colon, *last_colon, *end, *p = NULL;
	unsigned long tmp;

	*host = host_buf;

	first_colon = strchr(host_buf, ':');
	last_colon = strrchr(host_buf, ':');

	if (!first_colon) {
		/* IPv4 or name, default port */
	} else if (first_colon == last_colon) {
		/* Single colon, IPv4 non default port */
		*first_colon = '\0';
		p = first_colon + 1;
	} else if (*(last_colon - 1) == ']') {
		/* [IPv6]:port */
		(*host)++;
		*(last_colon - 1) = '\0';
		p = last_colon + 1;
	} else {
		/* IPv6 address with default port number */
	}

	if (!(*host)[0])
		return -EINVAL;

	if (!p)
		return dq_copy_str(port, port_len, DQ_DEFAULT_PORT);

	errno = 0;
	tmp = strtoul(p, &end, 10);
	if (p == end || *end || tmp > 0xFFFF || errno == ERANGE)
		return -EINVAL;

	iio_snprintf(port, port_len, "%lu", tmp);

	return 0;
}

static void dq_peer_addr(int fd, char *dst, size_t len, const char *fallback)
{
	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof(addr);

	if (getpeername(fd, (struct sockaddr *)&addr, &addrlen) ||
			getnameinfo((struct sockaddr *)&addr, addrlen, dst, (socklen_t)len, NULL, 0,
					NI_NUMERICHOST))
		iio_strlcpy(dst, fallback, len);
}

static int dq_add_buffer_attrs(struct iio_context *ctx);

static struct iio_context *daqiri_create_context(
		const struct iio_context_params *params, const char *uri)
{
	const char *ctx_attrs[] = {
		"uri",
		"ip,ip-addr",
		"daqiri,yaml",
		"daqiri,data",
		"daqiri,mem",
		"daqiri,engine",
		"daqiri,version",
	};
	const char *ctx_values[IIO_ARRAY_SIZE(ctx_attrs)];
	char description[sizeof("DAQIRI to ") + DQ_HOST_MAX];
	char ip_addr[INET6_ADDRSTRLEN + IF_NAMESIZE + 1];
	char host_buf[DQ_HOST_MAX], *host, *full_uri;
	const char *opts_str, *engine = "none";
	struct iio_context_pdata *pdata;
	struct iiod_client *client;
	struct iio_context *ctx;
	char err[256];
	size_t host_len;
	int ret;

	pdata = zalloc(sizeof(*pdata));
	if (!pdata)
		return iio_ptr(-ENOMEM);

	pdata->mgmt.fd = -1;
	pdata->mgmt.cancel_fd = -1;
	pdata->mgmt.params = params;

	ret = -pthread_mutex_init(&pdata->lock, NULL);
	if (ret) {
		free(pdata);
		return iio_ptr(ret);
	}

	/* Options first, so that a bad URI fails without touching the network */
	opts_str = strchr(uri, ',');
	ret = dq_opts_parse(&pdata->opts, opts_str ? opts_str + 1 : NULL, params);
	if (ret)
		goto err_free_pdata;

	host_len = opts_str ? (size_t)(opts_str - uri) : strlen(uri);
	ret = -EINVAL;
	if (host_len < sizeof(host_buf)) {
		memcpy(host_buf, uri, host_len);
		host_buf[host_len] = '\0';
		ret = dq_parse_host(host_buf, &host, pdata->port, sizeof(pdata->port));
	}
	if (ret) {
		prm_err(params, "daqiri: invalid host in \'%s\'\n", uri);
		goto err_free_pdata;
	}

	iio_strlcpy(pdata->host, host, sizeof(pdata->host));

	if (pdata->opts.yaml[0]) {
		ret = dq_yaml_validate(pdata->opts.yaml, err, sizeof(err));
		if (ret) {
			prm_err(params, "daqiri: invalid yaml %s: %s\n", pdata->opts.yaml, err);
			ret = -EINVAL;
			goto err_free_pdata;
		}

		engine = dq_yaml_engine_kind(pdata->opts.yaml);
	}
	pdata->engine_kind = engine;

	ret = dq_mgmt_connect(&pdata->mgmt, pdata->host, pdata->port, params->timeout_ms);
	if (ret)
		goto err_free_pdata;

	client = iiod_client_new(params, (struct iiod_client_pdata *)&pdata->mgmt, &dq_mgmt_ops);
	ret = iio_err(client);
	if (ret)
		goto err_mgmt_close;

	pdata->client = client;

	full_uri = malloc(sizeof("daqiri:") + strlen(uri));
	if (!full_uri) {
		ret = -ENOMEM;
		goto err_destroy_client;
	}
	iio_snprintf(full_uri, sizeof("daqiri:") + strlen(uri), "daqiri:%s", uri);

	dq_peer_addr(pdata->mgmt.fd, ip_addr, sizeof(ip_addr), pdata->host);
	iio_snprintf(description, sizeof(description), "DAQIRI to %s", pdata->host);

	ctx_values[0] = full_uri;
	ctx_values[1] = ip_addr;
	ctx_values[2] = pdata->opts.yaml;
	ctx_values[3] = dq_data_names[pdata->opts.data];
	ctx_values[4] = dq_mem_names[pdata->opts.mem];
	ctx_values[5] = engine;
	ctx_values[6] = dq_engine_version();

	prm_dbg(params, "Creating context...\n");
	ctx = iiod_client_create_context(client, &iio_daqiri_backend, description, ctx_attrs,
			ctx_values, IIO_ARRAY_SIZE(ctx_values));
	free(full_uri);
	ret = iio_err(ctx);
	if (ret)
		goto err_destroy_client;

	iio_context_set_pdata(ctx, pdata);

	/* pdata->mgmt.params points to the 'params' function argument,
	 * switch it to the context's copy now */
	pdata->mgmt.params = iio_context_get_params(ctx);

	/* A stock iiod drops the connection on a command it does not know:
	 * the setup goes only to a server that advertises UDP sample mode */
	pdata->udp_advertised = iio_context_find_attr(ctx, "iiod,udp-stream") &&
				iiod_client_uses_binary_interface(client);

	ret = dq_add_buffer_attrs(ctx);
	if (ret) {
		/* Shutdown releases pdata's resources, the core frees it */
		iio_context_destroy(ctx);
		return iio_ptr(ret);
	}

	return ctx;

err_destroy_client:
	dq_mgmt_ops.cancel((struct iiod_client_pdata *)&pdata->mgmt);
	iiod_client_destroy(client);
err_mgmt_close:
	dq_mgmt_close(&pdata->mgmt);
err_free_pdata:
	pthread_mutex_destroy(&pdata->lock);
	free(pdata);
	return iio_ptr(ret);
}

static struct iio_context_pdata *dq_dev_pdata(const struct iio_device *dev)
{
	return iio_context_get_pdata(iio_device_get_context(dev));
}

/*
 * iio_buffer_add_attr() without the sort: iio_add_attr() (attr.c) sorts the
 * list by name after every add, and iiod-client sends a buffer attribute's
 * position in that list as its index on the wire, which must be the
 * server's. Appended, the attributes that the server serves keep their
 * positions, and iio_buffer_find_attr(), a linear search, finds the
 * appended ones all the same. The entry is the one iio_add_attr() makes,
 * its filename the name itself, so that free_buffer() frees it.
 */
static int dq_buffer_append_attr(struct iio_buffer *buf, const char *name)
{
	struct iio_attr_list *list = &buf->attrlist;
	struct iio_attr *attrs;
	char *dup;

	dup = iio_strdup(name);
	if (!dup)
		return -ENOMEM;

	attrs = realloc(list->attrs, (list->num + 1) * sizeof(*attrs));
	if (!attrs) {
		free(dup);
		return -ENOMEM;
	}

	attrs[list->num] = (struct iio_attr){
		.iio.buf = buf,
		.type = IIO_ATTR_TYPE_BUFFER,
		.name = dup,
		.filename = dup,
	};
	list->attrs = attrs;
	list->num++;

	return 0;
}

/*
 * The daqiri_* attributes of every input buffer, served here rather than by
 * iiod: the data path the buffer uses (or would), and the counters of its UDP
 * stream. They go after the server's own attributes, whose positions in the
 * list are their indices on the wire (dq_buffer_append_attr()): a sorted
 * add would put every daqiri_* name before data_available and the rest. No
 * buffer attribute may be added with iio_buffer_add_attr() after them,
 * since it sorts the whole list again.
 */
static int dq_add_buffer_attrs(struct iio_context *ctx)
{
	unsigned int i, j, k;
	struct iio_buffer *buf;
	struct iio_device *dev;
	int ret;

	for (i = 0; i < iio_context_get_devices_count(ctx); i++) {
		dev = iio_context_get_device(ctx, i);

		for (j = 0; j < iio_device_get_buffers_count(dev); j++) {
			buf = iio_device_get_buffer(dev, j);
			if (iio_buffer_is_output(buf))
				continue;

			for (k = 0; k < dq_nb_attrs; k++) {
				ret = dq_buffer_append_attr(buf, dq_attr_names[k]);
				if (ret)
					return ret;
			}
		}
	}

	return 0;
}

static bool dq_is_local_attr(const struct iio_attr *attr)
{
	return attr->type == IIO_ATTR_TYPE_BUFFER && !iio_buffer_is_output(attr->iio.buf) &&
	       dq_attr_index(attr->name) >= 0;
}

/* The open buffer behind a buffer attribute; call with pdata->lock */
static struct iio_buffer_pdata *dq_find_open(
		struct iio_context_pdata *pdata, const struct iio_buffer *buf)
{
	struct iio_buffer_pdata *b;

	for (b = pdata->bufs; b; b = b->next)
		if (iio_device_get_buffer(b->dev, b->idx) == buf)
			return b;

	return NULL;
}

/* The decision table: which data path a buffer opened now gets */
static int dq_data_path(const struct iio_context_pdata *pdata, const struct iio_buffer *buf,
		enum dq_path *path)
{
	bool output = iio_buffer_is_output(buf);

	switch (pdata->opts.data) {
	case DQ_DATA_TCP:
		*path = DQ_PATH_TCP;
		return 0;
	case DQ_DATA_UDP:
		/* UDP sample mode is receive only */
		if (output || !pdata->udp_advertised)
			return -ENOTSUP;
		if (!pdata->opts.yaml[0])
			return -EINVAL;
		*path = DQ_PATH_UDP;
		return 0;
	case DQ_DATA_AUTO:
	default:
		*path = !output && pdata->udp_advertised && pdata->opts.yaml[0] ? DQ_PATH_UDP
										: DQ_PATH_TCP;
		return 0;
	}
}

static ssize_t dq_local_attr_read(
		struct iio_context_pdata *pdata, const struct iio_attr *attr, char *dst, size_t len)
{
	struct iio_buffer_pdata *b;
	enum dq_path path;
	bool is_open;
	ssize_t ret;

	pthread_mutex_lock(&pdata->lock);
	b = dq_find_open(pdata, attr->iio.buf);
	if (b && b->path == DQ_PATH_UDP) {
		ret = dq_udp_read_attr(b->udp, attr->name, dst, len);
		pthread_mutex_unlock(&pdata->lock);
		return ret;
	}
	/* Under the lock: once it is released, a close may free b */
	is_open = b != NULL;
	path = is_open ? b->path : DQ_PATH_TCP;
	pthread_mutex_unlock(&pdata->lock);

	/* Over TCP, or no stream open: what one would start with */
	if (!is_open && dq_data_path(pdata, attr->iio.buf, &path))
		path = DQ_PATH_TCP;

	return dq_attr_read_default(pdata, path, attr->name, dst, len);
}

static ssize_t dq_local_attr_write(struct iio_context_pdata *pdata, const struct iio_attr *attr,
		const char *src, size_t len)
{
	struct iio_buffer_pdata *b;
	int ret = -EBADF; /* only an open UDP stream has a daqiri_lossy */

	pthread_mutex_lock(&pdata->lock);
	b = dq_find_open(pdata, attr->iio.buf);
	if (b && b->path == DQ_PATH_UDP)
		ret = dq_udp_write_attr(b->udp, attr->name, src, len);
	pthread_mutex_unlock(&pdata->lock);

	return ret ? ret : (ssize_t)len;
}

static ssize_t daqiri_read_attr(const struct iio_attr *attr, char *dst, size_t len)
{
	struct iio_context_pdata *pdata = dq_dev_pdata(iio_attr_get_device(attr));

	if (dq_is_local_attr(attr))
		return dq_local_attr_read(pdata, attr, dst, len);

	return iiod_client_attr_read(pdata->client, attr, dst, len);
}

static ssize_t daqiri_write_attr(const struct iio_attr *attr, const char *src, size_t len)
{
	struct iio_context_pdata *pdata = dq_dev_pdata(iio_attr_get_device(attr));

	if (dq_is_local_attr(attr))
		return dq_local_attr_write(pdata, attr, src, len);

	return iiod_client_attr_write(pdata->client, attr, src, len);
}

static const struct iio_device *daqiri_get_trigger(const struct iio_device *dev)
{
	return iiod_client_get_trigger(dq_dev_pdata(dev)->client, dev);
}

static int daqiri_set_trigger(const struct iio_device *dev, const struct iio_device *trigger)
{
	return iiod_client_set_trigger(dq_dev_pdata(dev)->client, dev, trigger);
}

static void daqiri_shutdown(struct iio_context *ctx)
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);
	struct iio_buffer_pdata *buf;

	/* UDP buffers the app left open: join their pollers, close them on
	 * the server and give their queues back; the app can no longer reach
	 * them past this point. TCP buffers are left as ip: leaves them. */
	while ((buf = pdata->bufs)) {
		pdata->bufs = buf->next;
		if (buf->path == DQ_PATH_UDP) {
			dq_udp_close_buffer(buf);
			free(buf);
		}
	}
	if (pdata->engine)
		dq_engine_release(pdata->engine);
	pthread_mutex_destroy(&pdata->lock);

	/* The core frees pdata itself after this returns */
	dq_mgmt_ops.cancel((struct iiod_client_pdata *)&pdata->mgmt);
	iiod_client_destroy(pdata->client);
	dq_mgmt_close(&pdata->mgmt);
}

static int daqiri_set_timeout(struct iio_context *ctx, int timeout)
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);
	int ret;

	ret = iiod_client_set_timeout(pdata->client, timeout);
	if (ret < 0) {
		ctx_perror(ctx, ret, "Unable to set R/W timeout");
		return ret;
	}

	return 0;
}

static struct iio_event_stream_pdata *daqiri_open_events_fd(const struct iio_device *dev)
{
	return iiod_client_open_event_stream(dq_dev_pdata(dev)->client, dev);
}

static int daqiri_reg_read(const struct iio_device *dev, uint32_t address, uint32_t *value)
{
	struct iiod_client *client = dq_dev_pdata(dev)->client;

	if (!iiod_client_uses_binary_interface(client)) {
		/* Fallback to attribute-based method for ASCII protocol */
		return -ENOSYS;
	}

	return iiod_client_reg_read(client, dev, address, value);
}

static int daqiri_reg_write(const struct iio_device *dev, uint32_t address, uint32_t value)
{
	struct iiod_client *client = dq_dev_pdata(dev)->client;

	if (!iiod_client_uses_binary_interface(client)) {
		/* Fallback to attribute-based method for ASCII protocol */
		return -ENOSYS;
	}

	return iiod_client_reg_write(client, dev, address, value);
}

static int daqiri_refresh_format(const struct iio_channel *chn)
{
	const struct iio_device *dev = iio_channel_get_device(chn);
	struct iio_context_pdata *pdata = dq_dev_pdata(dev);
	struct iio_data_format new_format;
	struct iio_channel *chn_rw = (struct iio_channel *)chn;
	char format_str[256];
	ssize_t ret;

	if (!iiod_client_uses_binary_interface(pdata->client))
		return -ENOTSUP;

	ret = iiod_client_refresh_format(pdata->client, dev, chn, format_str, sizeof(format_str));
	if (ret < 0)
		return (int)ret;

	/* The length is the server's reply code, not what was stored */
	if ((size_t)ret >= sizeof(format_str))
		return -EIO;

	format_str[ret] = '\0';

	ret = iio_parse_format_string(format_str, &new_format);
	if (ret < 0)
		return (int)ret;

	new_format.with_scale = chn->format.with_scale;
	new_format.scale = chn->format.scale;
	new_format.offset = chn->format.offset;

	chn_rw->format = new_format;

	return 0;
}

static int daqiri_ping(struct iio_context *ctx)
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_nop(pdata->client);
}

/*
 * Each buffer gets a TCP connection and iiod client of its own, exactly as
 * ip: does (network.c), so blocking buffer I/O never stalls the management
 * connection. A TCP buffer streams on it; a UDP buffer sends its setup and
 * ENABLE/DISABLE_BUFFER there.
 */
int dq_iiod_open_buffer(struct iio_buffer_pdata *buf, struct iio_channels_mask *mask)
{
	const struct iio_context_params *params =
			iio_context_get_params(iio_device_get_context(buf->dev));
	struct iio_context_pdata *pdata = buf->ctx;
	struct iiod_client_pdata *io = (struct iiod_client_pdata *)&buf->iiod.io;
	char port[sizeof(pdata->port)];
	int ret;

	if (pdata->opts.data_port)
		iio_snprintf(port, sizeof(port), "%u", (unsigned int)pdata->opts.data_port);
	else
		iio_strlcpy(port, pdata->port, sizeof(port));

	buf->iiod.io.params = params;

	/* Logs its own errors */
	ret = dq_mgmt_connect(&buf->iiod.io, pdata->host, port, params->timeout_ms);
	if (ret)
		return ret;

	buf->iiod.client = iiod_client_new(params, io, &dq_mgmt_ops);
	ret = iio_err(buf->iiod.client);
	if (ret) {
		dev_perror(buf->dev, ret, "Unable to create IIOD client");
		buf->iiod.client = NULL;
		goto err_close;
	}

	buf->iiod.pdata = iiod_client_open_buffer(
			buf->iiod.client, pdata->client, buf->dev, buf->idx, mask);
	ret = iio_err(buf->iiod.pdata);
	if (ret) {
		dev_perror(buf->dev, ret, "Unable to open buffer");
		buf->iiod.pdata = NULL;
		goto err_destroy_client;
	}

	return 0;

err_destroy_client:
	dq_mgmt_ops.cancel(io);
	iiod_client_destroy(buf->iiod.client);
	buf->iiod.client = NULL;
err_close:
	dq_mgmt_close(&buf->iiod.io);
	return ret;
}

void dq_iiod_close_buffer(struct iio_buffer_pdata *buf)
{
	if (buf->iiod.pdata)
		iiod_client_close_buffer(buf->iiod.pdata);
	buf->iiod.pdata = NULL;

	if (buf->iiod.client)
		iiod_client_destroy(buf->iiod.client);
	buf->iiod.client = NULL;

	dq_mgmt_close(&buf->iiod.io);
}

static struct iio_buffer_pdata *daqiri_open_buffer(
		const struct iio_device *dev, unsigned int idx, struct iio_channels_mask *mask)
{
	struct iio_context_pdata *pdata = dq_dev_pdata(dev);
	struct iio_buffer_pdata *buf;
	enum dq_path path;
	int ret;

	ret = dq_data_path(pdata, iio_device_get_buffer(dev, idx), &path);
	if (ret == -ENOTSUP) {
		dev_err(dev, "data=udp: %s\n",
				iio_buffer_is_output(iio_device_get_buffer(dev, idx))
						? "UDP sample mode is receive only"
						: "the server does not offer UDP sample mode");
		return iio_ptr(ret);
	}
	if (ret) {
		dev_err(dev, "data=udp needs yaml= or IIO_DAQIRI_YAML\n");
		return iio_ptr(ret);
	}

	buf = zalloc(sizeof(*buf));
	if (!buf)
		return iio_ptr(-ENOMEM);

	buf->ctx = pdata;
	buf->dev = dev;
	buf->idx = idx;
	buf->path = path;
	buf->iiod.io.fd = -1;
	buf->iiod.io.cancel_fd = -1;

	/* A UDP buffer may come back as a TCP one: the server refused it and
	 * data=auto */
	if (path == DQ_PATH_UDP)
		ret = dq_udp_open_buffer(buf, mask);
	else
		ret = dq_iiod_open_buffer(buf, mask);
	if (ret) {
		free(buf);
		return iio_ptr(ret);
	}

	pthread_mutex_lock(&pdata->lock);
	buf->next = pdata->bufs;
	pdata->bufs = buf;
	pthread_mutex_unlock(&pdata->lock);

	return buf;
}

static void daqiri_close_buffer(struct iio_buffer_pdata *buf)
{
	struct iio_context_pdata *pdata = buf->ctx;
	struct iio_buffer_pdata **p;

	pthread_mutex_lock(&pdata->lock);
	for (p = &pdata->bufs; *p; p = &(*p)->next) {
		if (*p == buf) {
			*p = buf->next;
			break;
		}
	}
	pthread_mutex_unlock(&pdata->lock);

	/* The core has cancelled the buffer already */
	if (buf->path == DQ_PATH_UDP)
		dq_udp_close_buffer(buf);
	else
		dq_iiod_close_buffer(buf);
	free(buf);
}

static int daqiri_enable_buffer(
		struct iio_buffer_pdata *buf, size_t nb_samples, bool enable, bool cyclic)
{
	if (buf->path == DQ_PATH_UDP)
		return dq_udp_enable_buffer(buf, nb_samples, enable);

	return iiod_client_enable_buffer(buf->iiod.pdata, nb_samples, enable, cyclic);
}

/* Wakes whatever is blocked on the buffer; its connection stays unusable
 * afterwards, and with UDP so does the stream, which ends with
 * CLOSE_BUFFER. */
static void daqiri_cancel_buffer(struct iio_buffer_pdata *buf)
{
	if (buf->path == DQ_PATH_UDP)
		dq_udp_cancel_buffer(buf);
	else
		dq_mgmt_ops.cancel((struct iiod_client_pdata *)&buf->iiod.io);
}

/* readbuf/writebuf: the ASCII-protocol path, used when create_block is
 * -ENOSYS; UDP blocks are always native */
static ssize_t daqiri_readbuf(struct iio_buffer_pdata *buf, void *dst, size_t len)
{
	if (buf->path == DQ_PATH_UDP)
		return -ENOSYS;

	return iiod_client_readbuf(buf->iiod.pdata, dst, len);
}

static ssize_t daqiri_writebuf(struct iio_buffer_pdata *buf, const void *src, size_t len)
{
	if (buf->path == DQ_PATH_UDP)
		return -ENOSYS;

	return iiod_client_writebuf(buf->iiod.pdata, src, len);
}

static struct iio_block_pdata *daqiri_create_block(
		struct iio_buffer_pdata *buf, size_t size, void **data)
{
	struct iio_block_pdata *block, *iiod_block;
	struct dq_udp_block *udp;
	int ret;

	if (buf->path == DQ_PATH_UDP) {
		block = zalloc(sizeof(*block));
		if (!block)
			return iio_ptr(-ENOMEM);

		udp = dq_udp_block_new(buf->udp, size, data);
		ret = iio_err(udp);
		if (ret) {
			free(block);
			return iio_ptr(ret);
		}

		block->buf = buf;
		block->udp = udp;
		return block;
	}

	/* -ENOSYS from an ASCII-only iiod makes the core malloc the block and
	 * fall back to readbuf/writebuf */
	iiod_block = iiod_client_create_block(buf->iiod.pdata, size, data);
	ret = iio_err(iiod_block);
	if (ret)
		return iio_ptr(ret);

	block = zalloc(sizeof(*block));
	if (!block) {
		iiod_client_free_block(iiod_block);
		return iio_ptr(-ENOMEM);
	}

	block->buf = buf;
	block->iiod_block = iiod_block;

	return block;
}

static void daqiri_free_block(struct iio_block_pdata *block)
{
	if (block->udp)
		dq_udp_block_free(block->buf->udp, block->udp);
	else
		iiod_client_free_block(block->iiod_block);
	free(block);
}

static int daqiri_enqueue_block(struct iio_block_pdata *block, size_t bytes_used, bool cyclic)
{
	if (block->udp) {
		/* No cyclic RX */
		if (cyclic)
			return -ENOTSUP;
		return dq_udp_enqueue(block->buf->udp, block->udp, bytes_used);
	}

	return iiod_client_enqueue_block(block->iiod_block, bytes_used, cyclic);
}

static int daqiri_dequeue_block(struct iio_block_pdata *block, bool nonblock)
{
	const struct iio_context_params *params;

	if (block->udp) {
		/* Read every time: iio_context_set_timeout() may change it */
		params = iio_context_get_params(iio_device_get_context(block->buf->dev));
		return dq_udp_dequeue(block->buf->udp, block->udp, nonblock, params->timeout_ms);
	}

	return iiod_client_dequeue_block(block->iiod_block, nonblock);
}

static int daqiri_get_dmabuf_fd(struct iio_block_pdata *block)
{
	return -ENOTSUP;
}

static int daqiri_disable_cpu_access(struct iio_block_pdata *block, bool disable)
{
	return -ENOSYS;
}

static const struct iio_backend_ops daqiri_ops = {
	.create = daqiri_create_context,
	.read_attr = daqiri_read_attr,
	.write_attr = daqiri_write_attr,
	.get_trigger = daqiri_get_trigger,
	.set_trigger = daqiri_set_trigger,
	.shutdown = daqiri_shutdown,
	.set_timeout = daqiri_set_timeout,

	.open_buffer = daqiri_open_buffer,
	.close_buffer = daqiri_close_buffer,
	.enable_buffer = daqiri_enable_buffer,
	.cancel_buffer = daqiri_cancel_buffer,

	.readbuf = daqiri_readbuf,
	.writebuf = daqiri_writebuf,

	.create_block = daqiri_create_block,
	.free_block = daqiri_free_block,
	.enqueue_block = daqiri_enqueue_block,
	.dequeue_block = daqiri_dequeue_block,

	.get_dmabuf_fd = daqiri_get_dmabuf_fd,
	.disable_cpu_access = daqiri_disable_cpu_access,

	.open_ev = daqiri_open_events_fd,
	.close_ev = iiod_client_close_event_stream,
	.read_ev = iiod_client_read_event,

	.reg_read = daqiri_reg_read,
	.reg_write = daqiri_reg_write,

	.refresh_format = daqiri_refresh_format,

	.ping = daqiri_ping,
};

__api_export_if(WITH_DAQIRI_BACKEND) const struct iio_backend iio_daqiri_backend = {
	.api_version = IIO_BACKEND_API_V1,
	.name = "daqiri",
	.uri_prefix = "daqiri:",
	.ops = &daqiri_ops,
	.default_timeout_ms = DQ_TIMEOUT_MS,
};
