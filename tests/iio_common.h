/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * iio_common - Part of libIIO utilities
 *
 * Copyright (C) 2014-2020 Analog Devices, Inc.
 * Author: Paul Cercueil
 * */

#ifndef IIO_TESTS_COMMON_H
#define IIO_TESTS_COMMON_H

#include <getopt.h>
#include <stdint.h>

/*
 * internal buffers need to be big enough for attributes
 * coming back from the kernel. Because of virtual memory,
 * only the amount of ram that is needed is used.
 */
#define BUF_SIZE 16384

enum backend {
	IIO_LOCAL,
	IIO_XML,
	IIO_NETWORK,
	IIO_USB,
	IIO_URI,
	IIO_AUTO,
};

void * xmalloc(size_t n, const char *name);
char *cmn_strndup(const char *str, size_t n);

struct iio_context * autodetect_context(bool rtn, const char *name,
					const char *scan, int *err_code);
unsigned long int sanitize_clamp(const char *name, const char *argv,
	uint64_t min, uint64_t max);
int iio_device_enable_channel(const struct iio_device *dev, const char * channel, bool type);

/* optstring is a string containing the legitimate option characters.
 * If such a character is followed by a colon, the option  requires  an  argument.
 * Two colons mean an option takes an optional argument.
 */
#define COMMON_OPTIONS "hVn:x:u:a::S::T:"

struct iio_context * handle_common_opts(char * name, int argc,
	char * const argv[], const char *optstring,
	const struct option *options, const char *options_descriptions[],
	int *ret);
struct option * add_common_options(const struct option * longopts);
void usage(char *name, const struct option *options, const char *options_descriptions[]);
void version(char *name);

char ** dup_argv(char * name, int argc, char * argv[]);
void free_argw(int argc, char * argw[]);

uint64_t get_time_us(void);

/* https://pubs.opengroup.org/onlinepubs/009695399/basedefs/limits.h.html
 * {NAME_MAX} : Maximum number of bytes in a filename
 * {PATH_MAX} : Maximum number of bytes in a pathname
 * {PAGESIZE} : Size in bytes of a page
 * Too bad we work on non-POSIX systems
 */
#ifndef NAME_MAX
#define NAME_MAX 256
#endif


#endif /* IIO_TESTS_COMMON_H */
