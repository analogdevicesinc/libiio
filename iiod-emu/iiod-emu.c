// SPDX-License-Identifier: MIT
/*
 * iiod-emu - IIO hardware emulator
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include <errno.h>
#include <getopt.h>
#include <iio/iio-lock.h>
#include <iio/iio.h>
#include <iiod/xml-zstd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tinyiiod/tinyiiod.h>

/* Must come after network.h, which pulls in winsock2.h. */
#include "network.h"

#ifdef IIOD_EMU_UDP
#include <iio/iio-backend.h>
#include <iiod/udp-stream.h>

#include "../iio-private.h" /* the context's ops, for --udp-mock-fail-disable */
#include "udp-stream.h"
#endif

#ifdef _WIN32
#include <windows.h>
#define strncasecmp _strnicmp
#endif

#define DEFAULT_PORT 30431
#define BACKLOG 16

/* How long the server sits in select() before looking around. */
#define POLL_TIMEOUT_MS 200

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

struct client {
	struct client *next;
	struct iiod_emu *emu;
	struct iio_thrd *thrd;
	emu_socket sock;
	char peer[32];
	bool done;
};

struct iiod_emu {
	struct iio_context *ctx;
	void *xml;
	size_t xml_len;
	struct iio_mutex *lock;
	struct client *clients;
};

/*
 * Set when the user asks us to quit. A signal handler may touch very little, and
 * the Windows console handler runs on a thread of its own, so storing here is all
 * either of them does; main() looks at it once per trip round the accept loop.
 */
static volatile sig_atomic_t stop;

#ifdef _WIN32

static BOOL WINAPI console_handler(DWORD type)
{
	switch (type) {
	case CTRL_C_EVENT:
	case CTRL_CLOSE_EVENT:
		stop = 1;
		return TRUE;
	default:
		return FALSE;
	}
}

static void setup_signals(void)
{
	SetConsoleCtrlHandler(console_handler, TRUE);
}

#else

static void sig_handler(int sig)
{
	(void)sig;

	stop = 1;
}

static void set_handler(int sig)
{
	struct sigaction action;

	sigaction(sig, NULL, &action);
	action.sa_handler = sig_handler;
	sigaction(sig, &action, NULL);
}

static void setup_signals(void)
{
	set_handler(SIGINT);
	set_handler(SIGTERM);
}

#endif

static const struct option options[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "port", required_argument, NULL, 'p' },
#ifdef IIOD_EMU_UDP
	{ "udp", no_argument, NULL, EMU_UDP_OPT_UDP },
	{ "udp-from", required_argument, NULL, EMU_UDP_OPT_FROM },
	{ "udp-pps", required_argument, NULL, EMU_UDP_OPT_PPS },
	{ "udp-drop-every", required_argument, NULL, EMU_UDP_OPT_DROP_EVERY },
	{ "udp-drop-list", required_argument, NULL, EMU_UDP_OPT_DROP_LIST },
	{ "udp-drop-prob", required_argument, NULL, EMU_UDP_OPT_DROP_PROB },
	{ "udp-seed", required_argument, NULL, EMU_UDP_OPT_SEED },
	{ "udp-reorder-window", required_argument, NULL, EMU_UDP_OPT_REORDER_WINDOW },
	{ "udp-dup-every", required_argument, NULL, EMU_UDP_OPT_DUP_EVERY },
	{ "udp-json", required_argument, NULL, EMU_UDP_OPT_JSON },
	{ "udp-count", required_argument, NULL, EMU_UDP_OPT_COUNT },
	{ "udp-mock-provider", required_argument, NULL, EMU_UDP_OPT_MOCK_PROVIDER },
	{ "udp-mock-fail-disable", required_argument, NULL, EMU_UDP_OPT_MOCK_FAIL_DISABLE },
#endif
	{ NULL, 0, NULL, 0 },
};

/*
 * Backends that talk to real hardware. iiod-emu serves emulated hardware; wiring
 * it to a physical device would turn it into an unauthenticated proxy for that
 * device, and "local" would re-export the host's own IIO devices.
 *
 * The test has to be the scheme name, not whether the backend is built in:
 * WITH_NETWORK_BACKEND_DYNAMIC and friends move these very backends out of the
 * built-in table and onto the dynamic-module path, so "not built in" would let
 * "ip:" straight through in exactly that configuration.
 */
static const char *const denied_schemes[] = { "local", "ip", "serial", "usb" };

static void usage(void)
{
	printf("Usage: iiod-emu [OPTIONS] <uri>\n"
	       "Serve an emulated Libiio context over TCP.\n"
	       "\n"
	       "<uri> is a Libiio context URI:\n"
	       "\temu:<device.xml>\tEmulate the hardware described by an XML file\n"
	       "\t<module>:<args>\t\tLoad a hardware model from a dynamic backend\n"
	       "Backends that reach real hardware are refused:");
	for (size_t i = 0; i < ARRAY_SIZE(denied_schemes); i++)
		printf(" %s", denied_schemes[i]);
	printf("\n"
	       "\n"
	       "Options:\n"
	       "\t-h, --help\t\tDisplay this help message\n"
	       "\t-p, --port <port>\tPort to listen on (default: %d)\n",
			DEFAULT_PORT);
#ifdef IIOD_EMU_UDP
	emu_udp_usage();
#endif
}

static bool uri_is_allowed(const char *uri)
{
	const char *colon = strchr(uri, ':');
	size_t i, len;

	len = colon ? (size_t)(colon - uri) : 0;

	/*
	 * A one-character scheme is a Windows drive letter, not a backend, so
	 * treat "C:\\dev.xml" the same as a path with no scheme at all rather
	 * than letting it fail later as an unknown backend named "C".
	 */
	if (len < 2) {
		fprintf(stderr, "\"%s\" is not a context URI. Did you mean emu:%s ?\n\n", uri, uri);
		return false;
	}

	for (i = 0; i < ARRAY_SIZE(denied_schemes); i++) {
		if (len == strlen(denied_schemes[i]) && !strncasecmp(uri, denied_schemes[i], len)) {
			fprintf(stderr, "Refusing the \"%s\" backend: iiod-emu serves emulated hardware, not real devices.\n\n",
					denied_schemes[i]);
			return false;
		}
	}

	return true;
}

static bool parse_port(const char *str, uint16_t *port)
{
	unsigned long val;
	char *end;

	errno = 0;
	val = strtoul(str, &end, 10);

	if (end == str || *end != '\0' || errno == ERANGE || !val || val > UINT16_MAX)
		return false;

	*port = (uint16_t)val;

	return true;
}

static ssize_t emu_read_cb(struct iiod_pdata *pdata, void *buf, size_t len)
{
	struct client *client = (struct client *)pdata;

	return emu_socket_read(client->sock, buf, len);
}

static ssize_t emu_write_cb(struct iiod_pdata *pdata, const void *buf, size_t len)
{
	struct client *client = (struct client *)pdata;

	return emu_socket_write(client->sock, buf, len);
}

static int client_thrd(void *d)
{
	struct client *client = d;
	struct iiod_emu *emu = client->emu;

	printf("Client %s connected\n", client->peer);

	iiod_interpreter(emu->ctx, (struct iiod_pdata *)client, emu_read_cb, emu_write_cb, emu->xml,
			emu->xml_len);

	printf("Client %s disconnected\n", client->peer);

	/*
	 * Announce that we are done before closing, not after. reap_clients()
	 * decides whether to shutdown() this socket while holding the lock, so
	 * publishing <done> first is what tells it that the descriptor is gone;
	 * the other order would let it shut down a descriptor the OS has already
	 * handed to somebody else.
	 */
	iio_mutex_lock(emu->lock);
	client->done = true;
	iio_mutex_unlock(emu->lock);

	emu_socket_close(client->sock);

	return 0;
}

/*
 * Join and free every client thread that has finished, or with <wait_all> every
 * client there is. An unfinished client is normally sitting in recv() waiting
 * for its peer to ask something; on the way out nobody is going to ask, so break
 * the socket to make that recv() return, or the join below never completes.
 */
static void reap_clients(struct iiod_emu *emu, bool wait_all)
{
	struct client **prev, *client, *reap = NULL;

	iio_mutex_lock(emu->lock);
	for (prev = &emu->clients; (client = *prev);) {
		if (!wait_all && !client->done) {
			prev = &client->next;
			continue;
		}

		if (!client->done)
			emu_socket_shutdown(client->sock);

		*prev = client->next;
		client->next = reap;
		reap = client;
	}
	iio_mutex_unlock(emu->lock);

	while ((client = reap)) {
		reap = client->next;
		iio_thrd_join_and_destroy(client->thrd);
		free(client);
	}
}

static int spawn_client(struct iiod_emu *emu, emu_socket sock, const char *peer)
{
	struct client *client;
	int err;

	client = calloc(1, sizeof(*client));
	if (!client)
		return -ENOMEM;

	client->emu = emu;
	client->sock = sock;
	snprintf(client->peer, sizeof(client->peer), "%s", peer);

	iio_mutex_lock(emu->lock);
	client->next = emu->clients;
	emu->clients = client;
	iio_mutex_unlock(emu->lock);

	client->thrd = iio_thrd_create(client_thrd, client, "iiod-emu-client");
	err = iio_err(client->thrd);
	if (err) {
		iio_mutex_lock(emu->lock);
		emu->clients = client->next;
		iio_mutex_unlock(emu->lock);
		free(client);
		return err;
	}

	return 0;
}

#ifdef IIOD_EMU_UDP
/* With --udp, the sample files sit next to the XML of an emu: URI */
static bool udp_cfg_is_valid(const struct emu_udp_cfg *cfg, const char *uri)
{
	if (cfg->stream_opt && !cfg->udp) {
		fprintf(stderr, "The --udp-* options need --udp.\n\n");
		return false;
	}

	if (cfg->udp && cfg->mock_log) {
		fprintf(stderr, "--udp and --udp-mock-provider exclude each other.\n\n");
		return false;
	}

	if (cfg->mock_disables && !cfg->mock_log) {
		fprintf(stderr, "--udp-mock-fail-disable needs --udp-mock-provider.\n\n");
		return false;
	}

	if (cfg->udp && strncmp(uri, "emu:", strlen("emu:"))) {
		fprintf(stderr, "--udp streams the sample files of an emu: URI, not %s\n\n", uri);
		return false;
	}

	return true;
}
#endif

int main(int argc, char **argv)
{
	struct iio_context_params params = { .log_level = LEVEL_INFO };
	struct iiod_emu emu = { 0 };
	emu_socket srv = EMU_INVALID_SOCKET;
	uint16_t port = DEFAULT_PORT;
	int ret = EXIT_FAILURE;
	char err_str[256];
	const char *uri;
	int c, err;
#ifdef IIOD_EMU_UDP
	struct emu_udp_mock *mock = NULL;
	struct emu_udp_cfg udp_cfg;
	struct emu_udp *udp = NULL;

	emu_udp_cfg_init(&udp_cfg);
#endif

	/*
	 * Our progress messages are useless if they sit in a buffer until we
	 * exit, which is what happens as soon as stdout is not a tty. MSVC
	 * treats _IOLBF as _IOFBF, so go fully unbuffered.
	 */
	setvbuf(stdout, NULL, _IONBF, 0);

	while ((c = getopt_long(argc, argv, "+hp:", options, NULL)) != -1) {
		switch (c) {
		case 'h':
			usage();
			return EXIT_SUCCESS;
		case 'p':
			if (!parse_port(optarg, &port)) {
				fprintf(stderr, "Invalid port: %s\n\n", optarg);
				goto out_usage;
			}
			break;
		default:
#ifdef IIOD_EMU_UDP
			if (emu_udp_is_option(c)) {
				if (emu_udp_parse_option(&udp_cfg, c, optarg))
					goto out_usage;
				break;
			}
#endif
			goto out_usage;
		}
	}

	if (optind + 1 != argc) {
		fprintf(stderr, "Expected exactly one context URI.\n\n");
		goto out_usage;
	}

	uri = argv[optind];

	if (!uri_is_allowed(uri))
		goto out_usage;

#ifdef IIOD_EMU_UDP
	if (!udp_cfg_is_valid(&udp_cfg, uri))
		goto out_usage;
#endif

	if (emu_network_init())
		goto out_free_udp_cfg;

	err = iiod_init();
	if (err) {
		fprintf(stderr, "Unable to initialize tinyiiod\n");
		goto out_network_deinit;
	}

	emu.lock = iio_mutex_create();
	if (iio_err(emu.lock)) {
		fprintf(stderr, "Unable to create mutex\n");
		emu.lock = NULL;
		goto out_iiod_cleanup;
	}

	emu.ctx = iio_create_context(&params, uri);
	err = iio_err(emu.ctx);
	if (err) {
		iio_strerror(-err, err_str, sizeof(err_str));
		fprintf(stderr, "Unable to create context from %s: %s\n", uri, err_str);
		emu.ctx = NULL;
		goto out_mutex_destroy;
	}

#ifdef IIOD_EMU_UDP
	/* Before the XML is made: clients learn about UDP mode from it. The mock
	 * provider advertises it too, so that the daqiri: backend, which only
	 * sends the setup to a server that does, can run against it. */
	if (udp_cfg.udp || udp_cfg.mock_log) {
		err = iio_context_add_attr(emu.ctx, "iiod,udp-stream", "1");
		if (err) {
			iio_strerror(-err, err_str, sizeof(err_str));
			fprintf(stderr, "Unable to advertise UDP mode: %s\n", err_str);
			goto out_ctx_destroy;
		}
	}

	if (udp_cfg.udp) {
		udp = emu_udp_create(&udp_cfg, uri + strlen("emu:"));
		if (!udp) {
			fprintf(stderr, "Unable to set up UDP mode\n");
			goto out_ctx_destroy;
		}
	}

	if (udp_cfg.mock_log) {
		mock = emu_udp_mock_create(udp_cfg.mock_log);
		if (!mock)
			goto out_ctx_destroy;
		/* The DISABLE_BUFFERs that fail: the emulated device's own
		 * ops, wrapped, before anything uses them */
		if (udp_cfg.mock_disables)
			emu.ctx->ops = emu_udp_mock_device_ops(
					mock, emu.ctx->ops, udp_cfg.mock_fail_disable);
	}
#endif

	emu.xml = iiod_get_xml_zstd(emu.ctx, &emu.xml_len);
	if (!emu.xml) {
		fprintf(stderr, "Unable to serialize the context\n");
		goto out_ctx_destroy;
	}

	if (emu.xml_len > IIOD_MAX_XML_PAYLOAD) {
		fprintf(stderr, "Warning: the context description is %zu bytes, more than the %u a client will read; clients will fail to parse it.\n",
				emu.xml_len, (unsigned int)IIOD_MAX_XML_PAYLOAD);
	}

	srv = emu_socket_listen(port, BACKLOG);
	if (srv == EMU_INVALID_SOCKET)
		goto out_free_xml;

	printf("Emulating %u device(s) from %s\n", iio_context_get_devices_count(emu.ctx), uri);
	printf("Listening on port %u; connect with: iio_info -u ip:127.0.0.1:%u\n", port, port);

#ifdef IIOD_EMU_UDP
	if (udp) {
		iiod_udp_stream_set_ops(&emu_udp_ops, udp);
		printf("UDP sample mode on\n");
	} else if (mock) {
		iiod_udp_stream_set_ops(&emu_udp_mock_ops, mock);
		printf("UDP mock provider on, logging to %s\n", udp_cfg.mock_log);
	}
#endif

	/*
	 * Not any earlier: nothing above looks at <stop>, so a handler installed
	 * there would swallow the first Ctrl+C and leave the user with a process
	 * that ignores it.
	 */
	setup_signals();

	while (!stop) {
		char peer[32];
		emu_socket sock;
		bool fatal;
		int ready;

		reap_clients(&emu, false);

		ready = emu_socket_wait(srv, POLL_TIMEOUT_MS);
		if (ready < 0)
			break;
		if (!ready)
			continue;

		sock = emu_socket_accept(srv, peer, sizeof(peer), &fatal);
		if (sock == EMU_INVALID_SOCKET) {
			if (fatal)
				break;
			continue;
		}

		err = spawn_client(&emu, sock, peer);
		if (err) {
			iio_strerror(-err, err_str, sizeof(err_str));
			fprintf(stderr, "Unable to handle client %s: %s\n", peer, err_str);
			emu_socket_close(sock);
		}
	}

	/*
	 * Two ways out of the loop. Either we were asked to quit, and what follows
	 * is ordinary cleanup, or the listening socket is broken, which whoever
	 * noticed has already reported and which leaves <ret> at EXIT_FAILURE.
	 */
	if (stop) {
		printf("Shutting down\n");
		ret = EXIT_SUCCESS;
	}

	emu_socket_close(srv);
	/* Every client gone, every buffer and its stream released */
	reap_clients(&emu, true);
#ifdef IIOD_EMU_UDP
	iiod_udp_stream_set_ops(NULL, NULL);
#endif
out_free_xml:
	free(emu.xml);
out_ctx_destroy:
#ifdef IIOD_EMU_UDP
	emu_udp_destroy(udp);
	emu_udp_mock_destroy(mock);
#endif
	iio_context_destroy(emu.ctx);
out_mutex_destroy:
	iio_mutex_destroy(emu.lock);
out_iiod_cleanup:
	iiod_cleanup();
out_network_deinit:
	emu_network_deinit();
out_free_udp_cfg:
#ifdef IIOD_EMU_UDP
	emu_udp_cfg_free(&udp_cfg);
#endif

	return ret;

out_usage:
	usage();
	goto out_free_udp_cfg;
}
