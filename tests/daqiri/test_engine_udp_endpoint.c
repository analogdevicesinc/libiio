/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * Where the engine shim (daqiri-engine.cpp, built into this test) says the
 * UDP sample mode datagrams of a queue go to and come from, the endpoint the
 * backend puts in its setup command: dq_yaml_udp_endpoint(), the code path of
 * dq_engine_udp_endpoint() on a YAML that is only parsed, never initialised.
 *
 * - The socket engine: local_addr and remote_addr of the loopback YAML and of
 *   the ZCU102 one, which is also checked to parse.
 * - With --raw, a raw (ibverbs) engine: the UDP destination port and the IPv4
 *   addresses of the flow that steers to each queue. DAQIRI's parser keeps a
 *   flow's addresses in network order and its ports in host order; the shim
 *   must hand both over in host order, byte-swapped neither twice nor not at
 *   all. Only a DAQIRI built with the ibverbs engine parses that YAML. When
 *   DAQIRI says that the engine "is not available in this build", and only
 *   then, the check is skipped: the program exits with SKIP_EXIT, which ctest
 *   reports as not run. DAQIRI_REQUIRE_RAW_ENGINES=1 makes that a failure, for
 *   a build that must have the engine, as daqiri.yml's image will once it is
 *   rebuilt from CI/daqiri/Dockerfile (CI/daqiri/README.md).
 *
 * Usage: test_engine_udp_endpoint <resources dir> [--raw]
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../daqiri-private.h"
#include "test_framework.h"

#define IP(a, b, c, d) (((uint32_t)(a) << 24) | ((b) << 16) | ((c) << 8) | (d))

/* ctest's SKIP_RETURN_CODE for the raw check (tests/daqiri/CMakeLists.txt) */
#define SKIP_EXIT 77

/* What DAQIRI logs for a YAML whose engine its build left out (types.h) */
#define NOT_IN_BUILD "Engine 'ibverbs' is not available in this build"

struct endpoint {
	uint32_t local_ip, remote_ip;
	uint16_t local_port, remote_port;
};

static const char *dir;
static bool raw_skipped;

static int endpoint_of(const char *file, const char *iface, int queue, struct endpoint *ep)
{
	char path[4096];

	snprintf(path, sizeof(path), "%s/%s", dir, file);

	return dq_yaml_udp_endpoint(path, iface, queue, &ep->local_ip, &ep->local_port,
			&ep->remote_ip, &ep->remote_port);
}

static void print_endpoint(const char *what, const struct endpoint *ep)
{
	printf("  %s: local %u.%u.%u.%u:%u, remote %u.%u.%u.%u:%u\n", what, ep->local_ip >> 24,
			(ep->local_ip >> 16) & 0xff, (ep->local_ip >> 8) & 0xff,
			ep->local_ip & 0xff, ep->local_port, ep->remote_ip >> 24,
			(ep->remote_ip >> 16) & 0xff, (ep->remote_ip >> 8) & 0xff,
			ep->remote_ip & 0xff, ep->remote_port);
}

/* <ret> and every field once, through a variable: TEST_ASSERT_EQ() evaluates
 * its first argument twice */
static void check(const char *what, int ret, const struct endpoint *ep, const struct endpoint *want)
{
	char msg[160];
	long v;

	snprintf(msg, sizeof(msg), "%s: found", what);
	TEST_ASSERT_EQ(ret, 0, msg);
	if (ret)
		return;

	print_endpoint(what, ep);

	v = (long)ep->local_ip;
	snprintf(msg, sizeof(msg), "%s: local address", what);
	TEST_ASSERT_EQ(v, (long)want->local_ip, msg);
	v = ep->local_port;
	snprintf(msg, sizeof(msg), "%s: local port", what);
	TEST_ASSERT_EQ(v, (long)want->local_port, msg);
	v = (long)ep->remote_ip;
	snprintf(msg, sizeof(msg), "%s: remote address", what);
	TEST_ASSERT_EQ(v, (long)want->remote_ip, msg);
	v = ep->remote_port;
	snprintf(msg, sizeof(msg), "%s: remote port", what);
	TEST_ASSERT_EQ(v, (long)want->remote_port, msg);
}

TEST_FUNCTION(socket_engine)
{
	const struct endpoint loopback = { IP(127, 0, 0, 1), IP(127, 0, 0, 1), 25101, 25102 };
	const struct endpoint zcu102 = { IP(192, 0, 2, 2), IP(192, 0, 2, 1), 4991, 4991 };
	struct endpoint ep;
	int ret;

	ret = endpoint_of("socket-udp-loopback.yaml", "", 0, &ep);
	check("socket-udp-loopback.yaml queue 0", ret, &ep, &loopback);

	/* The interface by name and by address, as iface= takes it */
	ret = endpoint_of("zcu102-socket.yaml", "data", 0, &ep);
	check("zcu102-socket.yaml, interface data, queue 0", ret, &ep, &zcu102);
	ret = endpoint_of("zcu102-socket.yaml", "192.0.2.2", 0, &ep);
	check("zcu102-socket.yaml, interface 192.0.2.2, queue 0", ret, &ep, &zcu102);

	ret = endpoint_of("zcu102-socket.yaml", "", 1, &ep);
	TEST_ASSERT_EQ(ret, -EINVAL, "zcu102-socket.yaml has no queue 1");
	ret = endpoint_of("zcu102-socket.yaml", "eth9", 0, &ep);
	TEST_ASSERT_EQ(ret, -EINVAL, "zcu102-socket.yaml has no interface eth9");
}

/* dq_yaml_validate() of <path>, with what DAQIRI logged meanwhile, to stderr,
 * in <log> (and still on stderr) */
static int validate_logged(const char *path, char *err, size_t errlen, char *log, size_t loglen)
{
	FILE *tmp = tmpfile();
	int saved = -1, ret;
	size_t n = 0;

	fflush(stderr);
	if (tmp)
		saved = dup(STDERR_FILENO);
	if (saved >= 0 && dup2(fileno(tmp), STDERR_FILENO) < 0) {
		close(saved);
		saved = -1;
	}

	ret = dq_yaml_validate(path, err, errlen);

	fflush(stderr);
	if (saved >= 0) {
		dup2(saved, STDERR_FILENO);
		close(saved);
		rewind(tmp);
		n = fread(log, 1, loglen - 1, tmp);
	}
	if (tmp)
		fclose(tmp);
	log[n] = '\0';
	fputs(log, stderr);

	return ret;
}

TEST_FUNCTION(raw_flows)
{
	/* udp_dst 4991 is 0x137f, 32531 when swapped; 192.0.2.2 would be
	 * 2.2.0.192 */
	const struct endpoint q0 = { IP(192, 0, 2, 2), IP(192, 0, 2, 1), 4991, 4990 };
	const struct endpoint q1 = { IP(198, 51, 100, 7), 0, 6001, 0 };
	const char *require = getenv("DAQIRI_REQUIRE_RAW_ENGINES");
	char path[4096], err[256] = "", log[8192];
	bool required = require && !strcmp(require, "1");
	struct endpoint ep;
	int ret;

	snprintf(path, sizeof(path), "%s/raw-flows-ibverbs.yaml", dir);
	ret = validate_logged(path, err, sizeof(err), log, sizeof(log));
	if (ret && strstr(log, NOT_IN_BUILD) && !required) {
		printf("  SKIP: this DAQIRI build has no ibverbs engine (\"%s\"): the flows' byte "
		       "order is not checked. DAQIRI_REQUIRE_RAW_ENGINES=1 fails instead.\n",
				NOT_IN_BUILD);
		raw_skipped = true;
		return;
	}

	if (ret)
		printf("  raw-flows-ibverbs.yaml does not parse: %s%s\n", err,
				required ? " (DAQIRI_REQUIRE_RAW_ENGINES=1)" : "");
	TEST_ASSERT_EQ(ret, 0, "raw-flows-ibverbs.yaml parses");
	if (ret)
		return;

	ret = endpoint_of("raw-flows-ibverbs.yaml", "", 0, &ep);
	check("raw-flows-ibverbs.yaml queue 0", ret, &ep, &q0);
	ret = endpoint_of("raw-flows-ibverbs.yaml", "0000:ff:1f.7", 1, &ep);
	check("raw-flows-ibverbs.yaml queue 1", ret, &ep, &q1);
}

int main(int argc, char **argv)
{
	bool raw = argc == 3 && !strcmp(argv[2], "--raw");

	if (argc != 2 && !raw) {
		printf("Usage: %s <resources dir> [--raw]\n", argv[0]);
		return 2;
	}
	dir = argv[1];

	if (raw)
		RUN_TEST(raw_flows);
	else
		RUN_TEST(socket_engine);

	printf("test_engine_udp_endpoint: %d/%d passed, %d failed%s\n", test_passed, test_count,
			test_failed, raw_skipped ? ", skipped" : "");

	if (test_failed)
		return 1;

	return raw_skipped ? SKIP_EXIT : 0;
}
