// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * iio_common - Common functions used in the IIO utilities
 *
 * Copyright (C) 2014-2020 Analog Devices, Inc.
 * Author: Paul Cercueil
 * */

#include <iio.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <getopt.h>
#include <string.h>
#include <time.h>

#include "iio_common.h"
#include "gen_code.h"
#include "iio-config.h"

#ifdef _MSC_BUILD
#define inline __inline
#define iio_snprintf sprintf_s
#else
#define iio_snprintf snprintf
#endif

void * xmalloc(size_t n, const char * name)
{
	void *p = malloc(n);

	if (!p && n != 0) {
		if (name) {
			fprintf(stderr, "%s fatal error: allocating %zu bytes failed\n",
				name, n);
		} else {
			fprintf(stderr, "Fatal error: allocating %zu bytes failed\n", n);
		}
		exit(EXIT_FAILURE);
	}

	return p;
}

char *cmn_strndup(const char *str, size_t n)
{
#ifdef HAS_STRNDUP
	return strndup(str, n);
#else
	size_t len = strnlen(str, n + 1);
	char *buf = malloc(len + 1);
	if (buf) {
		/* len = size of buf, so memcpy is OK */
		memcpy(buf, str, len); /* Flawfinder: ignore */
		buf[len] = 0;
	}
	return buf;
#endif
}

struct iio_context * autodetect_context(bool rtn, const char * name,
					const char * scan, int *err_code)
{
	struct iio_scan_context *scan_ctx;
	struct iio_context_info **info;
	struct iio_context *ctx = NULL;
	unsigned int i;
	ssize_t ret;
	FILE *out;
	int err = EXIT_FAILURE;

	scan_ctx = iio_create_scan_context(scan, 0);
	if (!scan_ctx) {
		fprintf(stderr, "Unable to create scan context\n");
		return NULL;
	}

	ret = iio_scan_context_get_info_list(scan_ctx, &info);
	if (ret < 0) {
		char *err_str = xmalloc(BUF_SIZE, name);
		iio_strerror(-(int)ret, err_str, BUF_SIZE);
		fprintf(stderr, "Scanning for IIO contexts failed: %s\n", err_str);
		free (err_str);
		goto err_free_ctx;
	}

	if (ret == 0) {
		fprintf(stderr, "No IIO context found.\n");
		goto err_free_info_list;
	}
	if (rtn && ret == 1) {
		fprintf(stderr, "Using auto-detected IIO context at URI \"%s\"\n",
		iio_context_info_get_uri(info[0]));
		ctx = iio_create_context_from_uri(iio_context_info_get_uri(info[0]));
	} else {
		if (rtn) {
			out = stderr;
			fprintf(out, "Multiple contexts found. Please select one using --uri:\n");
		} else {
			out = stdout;
			fprintf(out, "Available contexts:\n");
			err = EXIT_SUCCESS;
		}
		for (i = 0; i < (size_t) ret; i++) {
			fprintf(out, "\t%u: %s [%s]\n",
					i, iio_context_info_get_description(info[i]),
					iio_context_info_get_uri(info[i]));
		}
	}

err_free_info_list:
	iio_context_info_list_free(info);
err_free_ctx:
	iio_scan_context_destroy(scan_ctx);

	if (err_code)
		*err_code = err;
	return ctx;
}

int iio_device_enable_channel(const struct iio_device *dev, const char * channel, bool type)
{
	struct iio_channel *ch;

	ch = iio_device_find_channel(dev, channel, type);
	if (!ch)
		return -ENXIO;

	if (iio_channel_is_enabled(ch))
		return -EBUSY;

	iio_channel_enable(ch);
	return 0;
}

unsigned long int sanitize_clamp(const char *name, const char *argv,
	uint64_t min, uint64_t max)
{
	uint64_t val;
	char buf[20], *end;

	if (!argv) {
		val = 0;
	} else {
		/* sanitized buffer by taking first 20 (or less) char */
		iio_snprintf(buf, sizeof(buf), "%s", argv);
		errno = 0;
		val = strtoul(buf, &end, 0);
		if (buf == end || errno == ERANGE)
			val = 0;
	}

	if (val > max) {
		val = max;
		fprintf(stderr, "Clamped %s to max %" PRIu64 "\n", name, max);
	}
	if (val < min) {
		val = min;
		fprintf(stderr, "Clamped %s to min %" PRIu64 "\n", name, min);
	}
	return (unsigned long int) val;
}

char ** dup_argv(char * name, int argc, char * argv[])
{
	int i;
	char** new_argv = xmalloc((argc + 1) * sizeof(char *), name);

	for(i = 0; i < argc; i++) {
		new_argv[i] = cmn_strndup(argv[i], NAME_MAX);
		if (!new_argv[i])
			goto err_dup;
	}

	return new_argv;

err_dup:
	i--;
	for (; i >= 0; i--)
		free(new_argv[i]);

	free(new_argv);

	fprintf(stderr, "out of memory\n");
	exit(0);
}

void free_argw(int argc, char * argw[])
{
	int i;

	for(i = 0; i < argc; i++) {
		free(argw[i]);
	}
	free(argw);
}

static const struct option common_options[] = {
	{"help", no_argument, 0, 'h'},
	{"version", no_argument, 0, 'V'},
	{"xml", required_argument, 0, 'x'},
	{"uri", required_argument, 0, 'u'},
	{"scan", optional_argument, 0, 'S'},
	{"auto", optional_argument, 0, 'a'},
	{"timeout", required_argument, 0, 'T'},
	{0, 0, 0, 0},
};

struct option * add_common_options(const struct option * longopts)
{
	int i = 0, j = 0;
	struct option *opts;

	while (longopts[i].name) {
		i++;
	}
	while (common_options[j].name) {
		j++;
		i++;
	}
	opts = calloc(i + 1, sizeof(struct option));
	if (!opts) {
		fprintf(stderr, "Out of memory\n");
		return NULL;
	}
	i = 0, j = 0;
	while (longopts[i].name) {
		opts[i].name = longopts[i].name;
		opts[i].has_arg = longopts[i].has_arg;
		opts[i].flag = longopts[i].flag;
		opts[i].val = longopts[i].val;
		i++;
	}
	while (common_options[j].name) {
		opts[i].name = common_options[j].name;
		opts[i].has_arg = common_options[j].has_arg;
		opts[i].flag = common_options[j].flag;
		opts[i].val = common_options[j].val;
		i++;
		j++;
	}

	return opts;
}

static const char *common_options_descriptions[] = {
	"Show this help and quit.",
	"Display libiio version information.",
	"Use the XML backend with the provided XML file.",
	("Use the context at the provided URI."
		"\n\t\t\teg: 'ip:192.168.2.1', 'ip:pluto.local', or 'ip:'"
		"\n\t\t\t    'usb:1.2.3', or 'usb:'"
		"\n\t\t\t    'serial:/dev/ttyUSB0,115200,8n1'"
		"\n\t\t\t    'local:' (Linux only)"),
	("Scan for available backends."
		"\n\t\t\toptional arg of specific backend(s)"
		"\n\t\t\t    'ip', 'usb' or 'ip:usb'"),
	("Scan for available contexts and if a single context is"
		"\n\t\t\tavailable use it. <arg> filters backend(s)"
		"\n\t\t\t    'ip', 'usb' or 'ip:usb:'"),
	("Context timeout in milliseconds."
		"\n\t\t\t0 = no timeout (wait forever)"),
};


struct iio_context * handle_common_opts(char * name, int argc,
		char * const argv[], const char *optstring,
		const struct option *options, const char *options_descriptions[],
		int *err_code)
{
	struct iio_context *ctx = NULL;
	int c;
	enum backend backend = IIO_LOCAL;
	const char *arg = NULL;
	bool do_scan = false, detect_context = false;
	char buf[128];
	struct option *opts;
	int timeout = -1;

	/* Setting opterr to zero disables error messages from getopt_long */
	opterr = 0;
	/* start over at first index */
	optind = 1;

	iio_snprintf(buf, sizeof(buf), "%s%s", COMMON_OPTIONS, optstring);
	opts = add_common_options(options);
	if (!opts) {
		fprintf(stderr, "Failed to add common options\n");
		goto err_fail;
	}

	while ((c = getopt_long(argc, argv, buf,	/* Flawfinder: ignore */
			opts, NULL)) != -1) {
		switch (c) {
		case 'h':
			usage(name, options, options_descriptions);
			break;
		case 'V':
			version(name);
			exit(0);
			break;
		case 'n':
			if (backend != IIO_LOCAL) {
				fprintf(stderr, "-a, -x, -n and -u are mutually exclusive\n");
				goto err_fail;
			}
			if (!optarg) {
				fprintf(stderr, "network options requires a uri\n");
				goto err_fail;
			}
			backend = IIO_NETWORK;
			arg = optarg;
			break;
		case 'x':
			if (backend != IIO_LOCAL) {
				fprintf(stderr, "-a, -x, -n and -u are mutually exclusive\n");
				goto err_fail;
			}
			if (!optarg) {
				fprintf(stderr, "xml options requires a uri\n");
				goto err_fail;
			}
			backend = IIO_XML;
			arg = optarg;
			break;
		case 'u':
			if (backend != IIO_LOCAL) {
				fprintf(stderr, "-a, -x, -n and -u are mutually exclusive\n");
				goto err_fail;
			}
			if (!optarg) {
				fprintf(stderr, "uri options requires a uri\n");
				goto err_fail;
			}
			backend = IIO_URI;
			arg = optarg;
			break;
		case 'a':
			if (backend != IIO_LOCAL) {
				fprintf(stderr, "-a, -x, -n and -u are mutually exclusive\n");
				goto err_fail;
			}
			backend = IIO_AUTO;
			detect_context = true;
			if (optarg) {
				arg = optarg;
			} else {
				if (argc > optind && argv[optind] && argv[optind][0] != '-')
					arg = argv[optind++];
			}
			break;
		case 'S':
			do_scan = true;
			if (optarg) {
				arg = optarg;
			} else {
				if (argc > optind && argv[optind] && argv[optind][0] != '-')
					arg = argv[optind++];
			}
			break;
		case 'T':
			if (!optarg) {
				fprintf(stderr, "Timeout requires an argument\n");
				goto err_fail;
			}
			timeout = sanitize_clamp("timeout", optarg, 0, INT_MAX);
			break;
		case '?':
			break;
		}
	}
	free(opts);
	optind = 1;
	opterr = 1;

	if (do_scan) {
		autodetect_context(false, name, arg, err_code);
		return NULL;
	} else if (detect_context || backend == IIO_AUTO)
		ctx = autodetect_context(true, name, arg, err_code);
	else if (!arg && backend != IIO_LOCAL)
		fprintf(stderr, "argument parsing error\n");
	else if (backend == IIO_XML)
		ctx = iio_create_xml_context(arg);
	else if (backend == IIO_NETWORK)
		ctx = iio_create_network_context(arg);
	else if (backend == IIO_URI)
		ctx = iio_create_context_from_uri(arg);
	else
		ctx = iio_create_default_context();

	if (!ctx && !do_scan && !detect_context) {
		char err_str[1024];
		iio_strerror(errno, err_str, sizeof(err_str));
		if (arg)
			fprintf(stderr, "Unable to create IIO context %s: %s\n", arg, err_str);
		else
			fprintf(stderr, "Unable to create Local IIO context : %s\n", err_str);
		goto err_fail;
	}

	if (ctx && timeout >= 0) {
		ssize_t ret = iio_context_set_timeout(ctx, timeout);
		if (ret < 0) {
			char err_str[1024];
			iio_strerror(-(int)ret, err_str, sizeof(err_str));
			fprintf(stderr, "IIO contexts set timeout failed : %s\n",
					err_str);
			iio_context_destroy(ctx);
			goto err_fail;
		}
	}

	return ctx;

err_fail:
	if (err_code)
		*err_code = EXIT_FAILURE;
	return NULL;
}

void usage(char *name, const struct option *options,
	const char *options_descriptions[])
{
	unsigned int i;

	printf("Usage:\n");
	printf("\t%s [OPTION]...\t%s\n", name, options_descriptions[0]);
	printf("Options:\n");
	for (i = 0; common_options[i].name; i++) {
		printf("\t-%c, --%s", common_options[i].val, common_options[i].name);
		if (common_options[i].has_arg == required_argument)
			printf(" [arg]");
		else if (common_options[i].has_arg == optional_argument)
			printf(" <arg>");
		printf("\n\t\t\t%s\n",
				common_options_descriptions[i]);
	}
	for (i = 0; options[i].name; i++) {
		printf("\t-%c, --%s", options[i].val, options[i].name);
		if (options[i].has_arg == required_argument)
			printf(" [arg]");
		else if (options[i].has_arg == optional_argument)
			printf(" <arg>");
		printf("\n\t\t\t%s\n",
			options_descriptions[i + 1]);

	}
	printf("\nThis is free software; see the source for copying conditions.  There is NO\n"
			"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\n");

	exit(0);
}

void version(char *name)
{
	unsigned int i, major, minor;
        char git_tag[8];

	printf("%s version: %u.%u (git tag:%s)\n", name, LIBIIO_VERSION_MAJOR, LIBIIO_VERSION_MINOR, LIBIIO_VERSION_GIT);
	iio_library_get_version(&major, &minor, git_tag);
        printf("Libiio version: %u.%u (git tag: %s) backends:", major, minor, git_tag);
        for (i = 0; i < iio_get_backends_count(); i++)
                printf(" %s", iio_get_backend(i));
        printf("\n");
}

uint64_t get_time_us(void)
{
	struct timespec tp;

#ifdef _MSC_BUILD
	timespec_get(&tp, TIME_UTC);
#else
	clock_gettime(CLOCK_REALTIME, &tp);
#endif

	return tp.tv_sec * 1000000ull + tp.tv_nsec / 1000;
}
