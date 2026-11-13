// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * The only translation unit of the DAQIRI backend that includes DAQIRI (C++)
 * headers. Every entry point is extern "C", catches everything and reports
 * failures as negative errno codes.
 */

#include <arpa/inet.h>
#include <cuda_runtime_api.h>
#include <daqiri/daqiri.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>

#include "daqiri-private.h"

/* The process-wide engine; never freed (its lifetime is the process's) */
struct dq_engine {
	std::string path; /* realpath of the YAML */
	daqiri::NetworkConfig cfg;
	unsigned int refs = 0;
	std::map<std::pair<int, int>, struct dq_rxq *> rxqs; /* by (port, queue) */
};

struct dq_rxq {
	dq_engine *e = nullptr;
	int port = 0, queue = 0;
	/* Packets start at the Ethernet header (dpdk, ibverbs) */
	bool raw = false;
	/* Header-data split: two memory regions, segment 0 then 1 */
	bool hds = false;

	/* Socket engine UDP: the socket's address as the YAML gives it (what
	 * DAQIRI keys its server sockets by), host order besides; the peer
	 * it is connected to, 0 when none */
	bool socket = false;
	std::string local_addr;
	uint32_t local_ip = 0, remote_ip = 0;
	uint16_t local_port = 0, remote_port = 0;

	/* Held across every DAQIRI call on this queue. Once the process
	 * exits, dead is set and nothing calls into DAQIRI any more. */
	std::mutex lock;
	bool dead = false;
};

namespace
{

int status_to_errno(daqiri::Status st)
{
	switch (st) {
	case daqiri::Status::SUCCESS:
		return 0;
	case daqiri::Status::NOT_READY:
		return -EAGAIN;
	case daqiri::Status::NO_FREE_BURST_BUFFERS:
	case daqiri::Status::NO_FREE_PACKET_BUFFERS:
		return -ENOBUFS;
	case daqiri::Status::INVALID_PARAMETER:
		return -EINVAL;
	case daqiri::Status::NOT_SUPPORTED:
		return -ENOTSUP;
	case daqiri::Status::RESOURCE_IN_USE:
		return -EBUSY;
	default:
		return -EIO;
	}
}

void set_err(char *err, size_t errlen, const char *msg)
{
	if (err && errlen)
		std::snprintf(err, errlen, "%s", msg);
}

/* Keep DAQIRI's INFO chatter out of a parse-only check; warnings and errors
 * still get through. The previous level is restored on exit. */
class quiet_info {
    public:
	quiet_info()
		: saved(daqiri::get_log_level())
	{
		if (saved < daqiri::LogSeverity::WARN)
			daqiri::set_log_level(daqiri::LogSeverity::WARN);
	}
	~quiet_info()
	{
		daqiri::set_log_level(saved);
	}
	quiet_info(const quiet_info &) = delete;
	quiet_info &operator=(const quiet_info &) = delete;

    private:
	daqiri::LogSeverity saved;
};

/* Guards g_engine, its rxqs and g_exiting */
std::mutex g_lock;
dq_engine *g_engine;
bool g_exiting;

/*
 * At exit, DAQIRI's static destructors shut the engine down while pollers
 * of buffers the application never closed may still be running. Registered
 * once daqiri_init() has returned, so that it runs before every destructor
 * DAQIRI registered until then: from here on no queue calls into DAQIRI.
 */
void dq_engine_atexit()
{
	std::lock_guard<std::mutex> lock(g_lock);

	g_exiting = true;
	if (!g_engine)
		return;

	for (auto &it : g_engine->rxqs) {
		std::lock_guard<std::mutex> qlock(it.second->lock);

		it.second->dead = true;
	}
}

/* DAQIRI logs its init at INFO; keep that quiet unless asked for. */
bool dq_debug()
{
	const char *env = std::getenv("IIO_DAQIRI_DEBUG");

	return env && env[0] && std::strcmp(env, "0");
}

/* Pin this module: the core dlclose()s it with the last context, but the
 * engine and its threads live until exit. */
int pin_module(char *err, size_t errlen)
{
	static bool pinned;
	Dl_info info;

	if (pinned)
		return 0;

	if (!dladdr((void *)&dq_engine_acquire, &info) || !info.dli_fname ||
			!dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE)) {
		const char *msg = dlerror();

		set_err(err, errlen, msg ? msg : "cannot pin the module");
		return -ELIBACC;
	}

	pinned = true;
	return 0;
}

/* What get_port_id() is asked for an interface of the YAML */
const std::string &iface_key(const daqiri::InterfaceConfig &ifc)
{
	return ifc.name_.empty() ? ifc.address_ : ifc.name_;
}

const daqiri::InterfaceConfig *find_iface(const dq_engine *e, int port)
{
	for (const auto &ifc : e->cfg.ifs_)
		if (daqiri::get_port_id(iface_key(ifc)) == port)
			return &ifc;

	return nullptr;
}

/* Before daqiri_init() resolves it, engine_type may still be UNKNOWN */
daqiri::EngineType engine_of(const daqiri::CommonConfig &c)
{
	return daqiri::is_explicit_engine_type(c.engine_type) ? c.engine_type : c.engine;
}

const daqiri::RxQueueConfig *find_rx_queue(const daqiri::InterfaceConfig &ifc, int queue_id)
{
	for (const auto &q : ifc.rx_.queues_)
		if (q.common_.id_ == queue_id)
			return &q;

	return nullptr;
}

/* Socket engine UDP queues: one datagram per packet, nothing before its
 * payload */
bool is_socket_udp(const daqiri::CommonConfig &c)
{
	return c.stream_type == daqiri::StreamType::SOCKET &&
	       c.protocol == daqiri::SocketProtocol::UDP &&
	       engine_of(c) != daqiri::EngineType::RDMA;
}

/* Raw queues of the dpdk and ibverbs engines: Ethernet frames */
bool is_raw(const daqiri::CommonConfig &c)
{
	daqiri::EngineType engine = engine_of(c);

	return c.stream_type == daqiri::StreamType::RAW &&
	       (engine == daqiri::EngineType::DPDK || engine == daqiri::EngineType::IBVERBS);
}

/* A dotted quad in host order; 0 for anything else */
uint32_t ipv4_of(const std::string &s)
{
	struct in_addr a;

	if (s.empty() || inet_pton(AF_INET, s.c_str(), &a) != 1)
		return 0;

	return ntohl(a.s_addr);
}

/* The network interface of the PCI device <bdf> (the address of a raw
 * engine's interface), "" when it has none, for example bound to DPDK */
std::string pci_netdev(const std::string &bdf)
{
	std::string name;
	struct dirent *de;
	DIR *d;

	if (bdf.empty() || bdf.find('/') != std::string::npos || bdf[0] == '.')
		return name;

	d = opendir(("/sys/bus/pci/devices/" + bdf + "/net").c_str());
	if (!d)
		return name;

	while ((de = readdir(d))) {
		if (de->d_name[0] != '.') {
			name = de->d_name;
			break;
		}
	}
	closedir(d);

	return name;
}

/* The IPv4 address of interface <name> (host order, 0 if none), or with an
 * empty <name> the name of the interface that has <ip> */
uint32_t netdev_ipv4(const std::string &name, uint32_t ip, std::string *found)
{
	struct ifaddrs *ifa, *it;
	uint32_t ret = 0;

	if (getifaddrs(&ifa))
		return 0;

	for (it = ifa; it; it = it->ifa_next) {
		uint32_t addr;

		if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET || !it->ifa_name)
			continue;

		addr = ntohl(((const struct sockaddr_in *)it->ifa_addr)->sin_addr.s_addr);
		if (!name.empty() ? name == it->ifa_name : addr == ip) {
			if (found)
				*found = it->ifa_name;
			ret = addr;
			break;
		}
	}

	freeifaddrs(ifa);

	return ret;
}

/* The IP MTU of interface <name>; 0 when unknown */
unsigned int netdev_mtu(const std::string &name)
{
	unsigned int mtu = 0;
	std::FILE *f;

	if (name.empty() || name.find('/') != std::string::npos)
		return 0;

	f = std::fopen(("/sys/class/net/" + name + "/mtu").c_str(), "r");
	if (!f)
		return 0;
	if (std::fscanf(f, "%u", &mtu) != 1)
		mtu = 0;
	std::fclose(f);

	return mtu;
}

bool targets_queue(const daqiri::FlowAction &a, int queue_id)
{
	if (a.type_ != daqiri::FlowType::QUEUE)
		return false;
	if (a.ids_.empty())
		return a.id_ == queue_id;

	return std::find(a.ids_.begin(), a.ids_.end(), queue_id) != a.ids_.end();
}

/* The first IPv4/UDP flow of the YAML that steers a UDP port to the queue */
const daqiri::FlowConfig *queue_flow(const daqiri::InterfaceConfig &ifc, int queue_id)
{
	for (const auto &f : ifc.rx_.flows_) {
		if (f.match_.type_ != daqiri::FlowMatchType::IPV4_UDP || !f.match_.udp_dst_)
			continue;

		for (const auto &a : daqiri::flow_config_actions(f))
			if (targets_queue(a, queue_id))
				return &f;
	}

	return nullptr;
}

/* The interface of the YAML named <iface>, by name or by address (a PCI BDF on
 * the raw engines, the socket address); empty or NULL: the first */
const daqiri::InterfaceConfig *iface_named(const daqiri::NetworkConfig &cfg, const char *iface)
{
	for (const auto &it : cfg.ifs_)
		if (!iface || !iface[0] || it.name_ == iface || it.address_ == iface)
			return &it;

	return nullptr;
}

/* dq_engine_udp_endpoint() for queue <queue_id> of interface <ifc> */
int udp_endpoint(const daqiri::CommonConfig &c, const daqiri::InterfaceConfig &ifc, int queue_id,
		uint32_t *local_ip, uint16_t *local_port, uint32_t *remote_ip,
		uint16_t *remote_port)
{
	const daqiri::FlowConfig *flow;

	*local_ip = *remote_ip = 0;
	*local_port = *remote_port = 0;

	if (!find_rx_queue(ifc, queue_id))
		return -EINVAL;

	if (is_socket_udp(c)) {
		/* The engine binds local_addr and connects to remote_addr */
		*local_ip = ipv4_of(ifc.socket_.local_ip_);
		*local_port = ifc.socket_.local_port_;
		*remote_ip = ipv4_of(ifc.socket_.remote_ip_);
		*remote_port = ifc.socket_.remote_port_;
		return 0;
	}

	if (!is_raw(c))
		return -ENOTSUP;

	/* The address of the kernel's interface on that port, which answers
	 * ARP for it, else whatever destination the flow names. DAQIRI's parser
	 * stores the flow's addresses in network order (in_addr_t, as
	 * inet_pton() gives them) and its ports in host order. */
	flow = queue_flow(ifc, queue_id);
	*local_ip = netdev_ipv4(pci_netdev(ifc.address_), 0, nullptr);
	if (flow) {
		if (!*local_ip)
			*local_ip = ntohl(flow->match_.ipv4_dst_);
		*local_port = flow->match_.udp_dst_;
		*remote_ip = ntohl(flow->match_.ipv4_src_);
		*remote_port = flow->match_.udp_src_;
	}

	return 0;
}

/* Parse only: never initialises the engine. */
int parse(const char *path, daqiri::NetworkConfig &cfg, char *err, size_t errlen)
{
	if (!path || !path[0]) {
		set_err(err, errlen, "no file given");
		return -EINVAL;
	}

	/* Report an unreadable file ourselves rather than through DAQIRI's log */
	std::FILE *f = std::fopen(path, "r");
	if (!f) {
		int ret = -errno;

		set_err(err, errlen, std::strerror(errno));
		return ret;
	}
	std::fclose(f);

	quiet_info quiet;
	daqiri::Status st = daqiri::parse_network_config_from_yaml_file(path, cfg);
	if (st != daqiri::Status::SUCCESS) {
		set_err(err, errlen, "rejected by the DAQIRI parser");
		return status_to_errno(st);
	}

	return 0;
}

/* The IPv4 address and port of socket <fd>'s own end, or of its peer;
 * false for anything else */
bool sock_ipv4(int fd, bool peer, uint32_t *ip, uint16_t *port)
{
	struct sockaddr_in sin;
	socklen_t len = sizeof(sin);

	if ((peer ? getpeername(fd, (struct sockaddr *)&sin, &len)
		  : getsockname(fd, (struct sockaddr *)&sin, &len)) ||
			len < sizeof(sin) || sin.sin_family != AF_INET)
		return false;

	*ip = ntohl(sin.sin_addr.s_addr);
	*port = ntohs(sin.sin_port);
	return true;
}

/* The IPv4 UDP socket of queue <q> among this process's file descriptors:
 * bound to its local_addr and, when the YAML names one, connected to its
 * remote_addr. Each candidate is duplicated first and checked through the
 * duplicate, so that a descriptor closed and reused meanwhile cannot be
 * mistaken for it. Returns that duplicate (close it), or -ENOENT when none or
 * more than one matches. */
int queue_socket_dup(const dq_rxq *q)
{
	int found = -ENOENT, nb = 0;
	struct dirent *de;
	DIR *d;

	d = opendir("/proc/self/fd");
	if (!d)
		return -errno;

	while ((de = readdir(d))) {
		int fd, dup, val;
		socklen_t len = sizeof(val);
		uint32_t ip;
		uint16_t port;
		char *end;
		bool match;

		errno = 0;
		fd = (int)std::strtol(de->d_name, &end, 10);
		if (de->d_name[0] == '.' || *end || errno || fd == dirfd(d))
			continue;

		dup = fcntl(fd, F_DUPFD_CLOEXEC, 0);
		if (dup < 0)
			continue;

		/* getsockopt() fails with ENOTSOCK for anything but a socket */
		match = !getsockopt(dup, SOL_SOCKET, SO_TYPE, &val, &len) && val == SOCK_DGRAM &&
			sock_ipv4(dup, false, &ip, &port) && port == q->local_port &&
			(!q->local_ip || ip == q->local_ip) &&
			(!q->remote_ip || (sock_ipv4(dup, true, &ip, &port) && ip == q->remote_ip &&
							  port == q->remote_port));
		if (!match) {
			close(dup);
			continue;
		}

		if (found >= 0)
			close(found);
		found = dup;
		nb++;
	}
	closedir(d);

	if (nb == 1)
		return found;
	if (found >= 0)
		close(found);
	return -ENOENT;
}

} // namespace

extern "C" int dq_yaml_validate(const char *path, char *err, size_t errlen)
{
	try {
		daqiri::NetworkConfig cfg{};

		return parse(path, cfg, err, errlen);
	} catch (const std::bad_alloc &) {
		set_err(err, errlen, "out of memory");
		return -ENOMEM;
	} catch (const std::exception &e) {
		set_err(err, errlen, e.what());
		return -EINVAL;
	} catch (...) {
		set_err(err, errlen, "unknown exception");
		return -EIO;
	}
}

extern "C" const char *dq_engine_version(void)
{
	return daqiri::version_string();
}

extern "C" const char *dq_yaml_engine_kind(const char *path)
{
	try {
		daqiri::NetworkConfig cfg{};
		const daqiri::CommonConfig &c = cfg.common_;

		if (parse(path, cfg, nullptr, 0))
			return "unknown";

		daqiri::EngineType engine = engine_of(c);

		switch (c.stream_type) {
		case daqiri::StreamType::SOCKET:
			if (c.protocol == daqiri::SocketProtocol::ROCE ||
					engine == daqiri::EngineType::RDMA)
				return "roce";
			return "socket";
		case daqiri::StreamType::RAW:
			if (engine == daqiri::EngineType::DPDK)
				return "dpdk";
			if (engine == daqiri::EngineType::IBVERBS)
				return "ibverbs";
			return "unknown";
		default:
			return "unknown";
		}
	} catch (...) {
		return "unknown";
	}
}

extern "C" int dq_engine_acquire(const char *yaml, struct dq_engine **out, char *err, size_t errlen)
{
	try {
		char real[PATH_MAX];
		daqiri::Status st;
		int ret;

		if (!yaml || !yaml[0]) {
			set_err(err, errlen, "no YAML given");
			return -EINVAL;
		}
		if (!realpath(yaml, real)) {
			ret = -errno;
			set_err(err, errlen, std::strerror(errno));
			return ret;
		}

		std::lock_guard<std::mutex> lock(g_lock);

		if (g_exiting)
			return -ESHUTDOWN;

		if (g_engine) {
			if (g_engine->path != real) {
				std::string msg = "this process runs DAQIRI from " + g_engine->path;

				set_err(err, errlen, msg.c_str());
				return -EBUSY;
			}
			g_engine->refs++;
			*out = g_engine;
			return 0;
		}

		ret = pin_module(err, errlen);
		if (ret)
			return ret;

		auto e = std::make_unique<dq_engine>();
		e->path = real;

		ret = parse(real, e->cfg, err, errlen);
		if (ret)
			return ret;

		if (dq_debug()) {
			st = daqiri::daqiri_init(e->cfg);
		} else {
			quiet_info quiet;
			st = daqiri::daqiri_init(e->cfg);
		}
		if (st != daqiri::Status::SUCCESS) {
			set_err(err, errlen, "daqiri_init failed");
			return status_to_errno(st);
		}

		/* Only fails without memory; streaming still works, only a
		 * process exiting with a buffer open is unguarded then */
		std::atexit(dq_engine_atexit);

		e->refs = 1;
		g_engine = e.release();
		*out = g_engine;
		return 0;
	} catch (const std::bad_alloc &) {
		set_err(err, errlen, "out of memory");
		return -ENOMEM;
	} catch (const std::exception &e) {
		set_err(err, errlen, e.what());
		return -EIO;
	} catch (...) {
		set_err(err, errlen, "unknown exception");
		return -EIO;
	}
}

extern "C" void dq_engine_release(struct dq_engine *e)
{
	try {
		std::lock_guard<std::mutex> lock(g_lock);

		/* No shutdown: the engine lives as long as the process */
		if (e && e->refs)
			e->refs--;
	} catch (...) {
	}
}

extern "C" int dq_engine_get_port(struct dq_engine *e, const char *iface, int *port_id)
{
	try {
		const daqiri::InterfaceConfig *ifc = iface_named(e->cfg, iface);
		int port;

		if (!ifc)
			return -EINVAL;

		port = daqiri::get_port_id(iface_key(*ifc));
		if (port < 0)
			return -EINVAL;

		*port_id = port;
		return 0;
	} catch (...) {
		return -EIO;
	}
}

extern "C" int dq_rxq_open(struct dq_engine *e, int port_id, int queue_id, struct dq_rxq **out)
{
	try {
		const daqiri::CommonConfig &c = e->cfg.common_;
		const daqiri::RxQueueConfig *qcfg;
		const daqiri::InterfaceConfig *ifc;
		bool raw;

		std::lock_guard<std::mutex> lock(g_lock);

		if (g_exiting)
			return -ESHUTDOWN;

		ifc = find_iface(e, port_id);
		if (!ifc)
			return -EINVAL;
		qcfg = find_rx_queue(*ifc, queue_id);
		if (!qcfg)
			return -EINVAL;

		/* Socket UDP: one datagram per packet, the datagram header
		 * first. Raw (dpdk, ibverbs): Ethernet frames, which the
		 * poller validates down to UDP. RoCE, TCP and RDMA carry no
		 * UDP sample mode datagrams. */
		if (is_socket_udp(c))
			raw = false;
		else if (is_raw(c))
			raw = true;
		else
			return -ENOTSUP;

		/* A raw queue with two regions splits every packet in two
		 * segments (header-data split); more are not supported */
		if (raw && qcfg->common_.mrs_.size() > 2)
			return -ENOTSUP;

		auto key = std::make_pair(port_id, queue_id);
		if (e->rxqs.count(key))
			return -EBUSY;

		auto q = std::make_unique<dq_rxq>();
		q->e = e;
		q->port = port_id;
		q->queue = queue_id;
		q->raw = raw;
		q->hds = raw && qcfg->common_.mrs_.size() == 2;
		if (!raw) {
			q->socket = true;
			q->local_addr = ifc->socket_.local_ip_;
			q->local_ip = ipv4_of(ifc->socket_.local_ip_);
			q->local_port = ifc->socket_.local_port_;
			q->remote_ip = ipv4_of(ifc->socket_.remote_ip_);
			q->remote_port = ifc->socket_.remote_port_;
		}
		e->rxqs[key] = q.get();
		*out = q.release();
		return 0;
	} catch (const std::bad_alloc &) {
		return -ENOMEM;
	} catch (...) {
		return -EIO;
	}
}

extern "C" int dq_engine_udp_endpoint(struct dq_engine *e, int port_id, int queue_id,
		uint32_t *local_ip, uint16_t *local_port, uint32_t *remote_ip,
		uint16_t *remote_port)
{
	try {
		const daqiri::InterfaceConfig *ifc = find_iface(e, port_id);

		*local_ip = *remote_ip = 0;
		*local_port = *remote_port = 0;

		if (!ifc)
			return -EINVAL;

		return udp_endpoint(e->cfg.common_, *ifc, queue_id, local_ip, local_port, remote_ip,
				remote_port);
	} catch (...) {
		return -EIO;
	}
}

extern "C" int dq_yaml_udp_endpoint(const char *yaml, const char *iface, int queue_id,
		uint32_t *local_ip, uint16_t *local_port, uint32_t *remote_ip,
		uint16_t *remote_port)
{
	try {
		daqiri::NetworkConfig cfg{};
		const daqiri::InterfaceConfig *ifc;
		int ret;

		*local_ip = *remote_ip = 0;
		*local_port = *remote_port = 0;

		ret = parse(yaml, cfg, nullptr, 0);
		if (ret)
			return ret;

		ifc = iface_named(cfg, iface);
		if (!ifc)
			return -EINVAL;

		return udp_endpoint(cfg.common_, *ifc, queue_id, local_ip, local_port, remote_ip,
				remote_port);
	} catch (const std::bad_alloc &) {
		return -ENOMEM;
	} catch (...) {
		return -EIO;
	}
}

extern "C" int dq_engine_rx_limits(
		struct dq_engine *e, int port_id, int queue_id, struct dq_rxq_limits *out)
{
	try {
		const daqiri::CommonConfig &c = e->cfg.common_;
		const daqiri::InterfaceConfig *ifc = find_iface(e, port_id);
		const daqiri::RxQueueConfig *qcfg;
		std::string netdev;

		std::memset(out, 0, sizeof(*out));

		qcfg = ifc ? find_rx_queue(*ifc, queue_id) : nullptr;
		if (!qcfg)
			return -EINVAL;
		if (!is_socket_udp(c) && !is_raw(c))
			return -ENOTSUP;

		/* With header-data split a packet takes one buffer of each */
		for (const auto &name : qcfg->common_.mrs_) {
			auto it = e->cfg.mrs_.find(name);

			if (it == e->cfg.mrs_.end())
				continue;
			out->buf_size += it->second.buf_size_;
			if (!out->num_bufs || it->second.num_bufs_ < out->num_bufs)
				out->num_bufs = it->second.num_bufs_;
		}

		out->raw = is_raw(c);
		if (out->raw) {
			netdev = pci_netdev(ifc->address_);
		} else {
			out->max_udp_payload = ifc->socket_.max_payload_size_;
			netdev_ipv4("", ipv4_of(ifc->socket_.local_ip_), &netdev);
		}
		out->mtu = netdev_mtu(netdev);

		return 0;
	} catch (...) {
		return -EIO;
	}
}

extern "C" void dq_rxq_close(struct dq_rxq *q)
{
	if (!q)
		return;

	try {
		std::lock_guard<std::mutex> lock(g_lock);

		q->e->rxqs.erase(std::make_pair(q->port, q->queue));
	} catch (...) {
	}
	delete q;
}

extern "C" int dq_rxq_set_rcvbuf(struct dq_rxq *q, int bytes, bool force)
{
	try {
		std::lock_guard<std::mutex> lock(q->lock);
		uint16_t port = 0, queue = 0;
		uintptr_t conn_id = 0;
		daqiri::Status st;

		if (q->dead)
			return -ESHUTDOWN;
		if (!q->socket)
			return -ENOTSUP;

		/* NULL_PTR: no server socket for that address, for example
		 * a UDP interface in client mode */
		st = daqiri::socket_get_server_conn_id(q->local_addr, q->local_port, &conn_id);
		if (st == daqiri::Status::NULL_PTR)
			return -ENOENT;
		if (st != daqiri::Status::SUCCESS)
			return status_to_errno(st);

		/* DAQIRI keys server sockets by address alone: with another
		 * interface on the same local_addr, the connection may be
		 * that interface's */
		st = daqiri::socket_get_port_queue(conn_id, &port, &queue);
		if (st != daqiri::Status::SUCCESS)
			return status_to_errno(st);
		if (port != q->port || queue != q->queue)
			return -EADDRINUSE;

		st = daqiri::socket_setsockopt(conn_id, SOL_SOCKET,
				force ? SO_RCVBUFFORCE : SO_RCVBUF, &bytes, sizeof(bytes));
		if (st == daqiri::Status::GENERIC_FAILURE)
			return -EIO;

		return status_to_errno(st);
	} catch (...) {
		return -EIO;
	}
}

extern "C" int dq_rxq_get_rcvbuf(struct dq_rxq *q, int *bytes)
{
	try {
		std::lock_guard<std::mutex> lock(q->lock);
		socklen_t len = sizeof(*bytes);
		int fd, ret;

		if (q->dead)
			return -ESHUTDOWN;
		if (!q->socket)
			return -ENOTSUP;

		fd = queue_socket_dup(q);
		if (fd < 0)
			return fd;

		ret = getsockopt(fd, SOL_SOCKET, SO_RCVBUF, bytes, &len) ? -errno : 0;
		close(fd);
		return ret;
	} catch (...) {
		return -EIO;
	}
}

extern "C" int dq_rxq_poll(struct dq_rxq *q, struct dq_burst *b)
{
	try {
		std::lock_guard<std::mutex> lock(q->lock);
		daqiri::BurstParams *burst = nullptr;
		daqiri::Status st;

		if (q->dead)
			return -ESHUTDOWN;

		st = daqiri::get_rx_burst(&burst, q->port, q->queue);

		if (st == daqiri::Status::NOT_READY || st == daqiri::Status::NULL_PTR ||
				(st == daqiri::Status::SUCCESS && !burst))
			return -EAGAIN;
		if (st != daqiri::Status::SUCCESS)
			return status_to_errno(st);

		b->q = q;
		b->handle = burst;
		b->nb_pkts = (unsigned)daqiri::get_num_packets(burst);
		b->raw = q->raw;
		return 0;
	} catch (...) {
		return -EIO;
	}
}

extern "C" unsigned dq_burst_pkts(const struct dq_burst *b, struct dq_pkt *out, unsigned max)
{
	auto *burst = static_cast<daqiri::BurstParams *>(b->handle);
	unsigned i, n = b->nb_pkts < max ? b->nb_pkts : max;

	try {
		std::lock_guard<std::mutex> lock(b->q->lock);

		if (b->q->dead)
			return 0;

		for (i = 0; i < n; i++) {
			void *seg0 = daqiri::get_segment_packet_ptr(burst, 0, (int)i);
			void *seg1 = b->q->hds ? daqiri::get_segment_packet_ptr(burst, 1, (int)i)
					       : nullptr;

			out[i].data = static_cast<const uint8_t *>(seg0);
			out[i].len = seg0 ? daqiri::get_segment_packet_length(burst, 0, (int)i) : 0;
			out[i].payload = static_cast<const uint8_t *>(seg1);
			out[i].payload_len =
					seg1 ? daqiri::get_segment_packet_length(burst, 1, (int)i)
					     : 0;
		}
	} catch (...) {
		return 0;
	}

	return n;
}

extern "C" void dq_burst_free(struct dq_burst *b)
{
	try {
		if (b->handle) {
			std::lock_guard<std::mutex> lock(b->q->lock);

			/* Exiting: the engine may be gone, leave the burst */
			if (!b->q->dead)
				daqiri::free_all_packets_and_burst_rx(
						static_cast<daqiri::BurstParams *>(b->handle));
		}
	} catch (...) {
	}
	b->handle = nullptr;
	b->nb_pkts = 0;
}

#define DQ_PAGE 4096
#define DQ_HUGE ((size_t)2 << 20)

/* <linux/mman.h> has it, but clashes with <sys/mman.h> */
#ifndef MAP_HUGE_2MB
#define MAP_HUGE_2MB (21 << MAP_HUGE_SHIFT)
#endif

extern "C" int dq_mem_alloc(enum dq_mem k, int gpu, size_t size, void **ptr, size_t *mapped)
{
	size_t len = (size + DQ_PAGE - 1) & ~(size_t)(DQ_PAGE - 1);
	cudaError_t ce;
	void *p = nullptr;

	(void)gpu; /* portable pinned memory is usable from every device */
	*mapped = 0;

	switch (k) {
	case DQ_MEM_HOST:
		if (size >= DQ_HUGE) {
			/* 2 MB hugetlb pages when the pool has enough free,
			 * faulted in now rather than on the first packet */
			size_t hlen = (size + DQ_HUGE - 1) & ~(DQ_HUGE - 1);

			p = mmap(nullptr, hlen, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_2MB |
							MAP_POPULATE,
					-1, 0);
			if (p != MAP_FAILED) {
				*mapped = hlen;
				break;
			}
		}

		p = aligned_alloc(DQ_PAGE, len ? len : DQ_PAGE);
		if (!p)
			return -ENOMEM;
		/* Advice only: fewer TLB misses where THP is enabled */
		madvise(p, len, MADV_HUGEPAGE);
		break;
	case DQ_MEM_PINNED:
		ce = cudaHostAlloc(&p, size ? size : 1, cudaHostAllocPortable);
		if (ce != cudaSuccess) {
			cudaGetLastError();
			return ce == cudaErrorMemoryAllocation ? -ENOMEM : -ENOTSUP;
		}
		break;
	default:
		/* mem=device: header-data split into GPU memory, a later phase */
		return -ENOTSUP;
	}

	*ptr = p;
	return 0;
}

extern "C" void dq_mem_free(enum dq_mem k, void *ptr, size_t mapped)
{
	switch (k) {
	case DQ_MEM_HOST:
		if (mapped)
			munmap(ptr, mapped);
		else
			free(ptr);
		break;
	case DQ_MEM_PINNED:
		if (ptr)
			cudaFreeHost(ptr);
		break;
	default:
		break;
	}
}
