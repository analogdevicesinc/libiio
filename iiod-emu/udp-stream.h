// SPDX-License-Identifier: MIT
/*
 * iiod-emu - IIO hardware emulator
 *
 * UDP sample mode (--udp): a reference server for the UDP path of the daqiri:
 * backend. Linux only (sendmmsg, clock_nanosleep): the build leaves it out
 * elsewhere.
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#ifndef IIOD_EMU_UDP_STREAM_H
#define IIOD_EMU_UDP_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct iio_backend_ops;
struct iiod_udp_stream_ops;

/* Long options, numbered past every short one */
enum emu_udp_option {
	EMU_UDP_OPT_UDP = 0x100,
	EMU_UDP_OPT_FROM,
	EMU_UDP_OPT_PPS,
	EMU_UDP_OPT_DROP_EVERY,
	EMU_UDP_OPT_DROP_LIST,
	EMU_UDP_OPT_DROP_PROB,
	EMU_UDP_OPT_SEED,
	EMU_UDP_OPT_REORDER_WINDOW,
	EMU_UDP_OPT_DUP_EVERY,
	EMU_UDP_OPT_JSON,
	EMU_UDP_OPT_COUNT,
	/* Hidden: the logging provider of the responder's contract test */
	EMU_UDP_OPT_MOCK_PROVIDER,
	EMU_UDP_OPT_MOCK_FAIL_DISABLE,
	EMU_UDP_OPT_END,
};

struct emu_udp_cfg {
	bool udp;	  /* --udp */
	bool stream_opt;  /* one of the options that only make sense with --udp */
	uint32_t from_ip; /* host order */
	uint16_t from_port;
	uint64_t pps; /* 0: as fast as possible */
	uint64_t drop_every;
	uint64_t *drop_list; /* sorted */
	size_t drop_list_len;
	double drop_prob;
	uint64_t seed;
	uint64_t reorder_window; /* 0: off */
	uint64_t dup_every;
	uint64_t count; /* sequence numbers per enable; 0: no end */
	const char *json;
	const char *mock_log;	    /* --udp-mock-provider */
	bool mock_disables;	    /* --udp-mock-fail-disable given */
	uint64_t mock_fail_disable; /* how many DISABLE_BUFFERs fail */
};

void emu_udp_cfg_init(struct emu_udp_cfg *cfg);
void emu_udp_cfg_free(struct emu_udp_cfg *cfg);

static inline bool emu_udp_is_option(int opt)
{
	return opt >= EMU_UDP_OPT_UDP && opt < EMU_UDP_OPT_END;
}

/* Take the value of option <opt>. Returns 0, or -EINVAL after saying why. */
int emu_udp_parse_option(struct emu_udp_cfg *cfg, int opt, const char *arg);

/* The options' help, for usage() */
void emu_udp_usage(void);

/*
 * The provider: it streams the sample file the emu backend serves for the
 * buffer, <dev>_buf<idx>.bin next to <xml_path>, as datagrams. <cfg> must
 * outlive it.
 */
struct emu_udp;
extern const struct iiod_udp_stream_ops emu_udp_ops;
struct emu_udp *emu_udp_create(const struct emu_udp_cfg *cfg, const char *xml_path);
void emu_udp_destroy(struct emu_udp *udp);

/*
 * A provider that sends nothing and writes one line per call to <path>, for
 * the test of the responder's rules (tests/daqiri/test_udp_provider_contract.c).
 */
struct emu_udp_mock;
extern const struct iiod_udp_stream_ops emu_udp_mock_ops;
struct emu_udp_mock *emu_udp_mock_create(const char *path);
void emu_udp_mock_destroy(struct emu_udp_mock *mock);

/*
 * --udp-mock-fail-disable <n>: the emulated device's <ops>, but for an
 * enable_buffer() that answers the first <n> DISABLE_BUFFERs with -ETIMEDOUT,
 * after the stream's stop(), and has the mock log every DISABLE_BUFFER. They
 * replace the context's own before the server takes connections.
 */
const struct iio_backend_ops *emu_udp_mock_device_ops(
		struct emu_udp_mock *mock, const struct iio_backend_ops *ops, uint64_t n);

#endif /* IIOD_EMU_UDP_STREAM_H */
