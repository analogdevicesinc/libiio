// SPDX-License-Identifier: MIT
/*
 * iiod-emu - IIO hardware emulator
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_PORT 30431

static const struct option options[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "port", required_argument, NULL, 'p' },
	{ NULL, 0, NULL, 0 },
};

static void usage(void)
{
	printf("Usage: iiod-emu [OPTIONS]\n"
	       "Options:\n"
	       "\t-h, --help\t\tDisplay this help message\n"
	       "\t-p, --port <port>\tPort to listen on (default: %u)\n",
			DEFAULT_PORT);
}

int main(int argc, char **argv)
{
	unsigned long port = DEFAULT_PORT;
	int c;

	while ((c = getopt_long(argc, argv, "+hp:", options, NULL)) != -1) {
		switch (c) {
		case 'h':
			usage();
			return EXIT_SUCCESS;
		case 'p':
			port = strtoul(optarg, NULL, 10);
			break;
		default:
			return EXIT_FAILURE;
		}
	}

	fprintf(stderr, "iiod-emu: not implemented yet (port %lu)\n", port);

	return EXIT_SUCCESS;
}
