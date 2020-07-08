/*
 * iio_common - Part of libIIO utilities
 *
 * Copyright (C) 2014-2020 Analog Devices, Inc.
 * Author: Paul Cercueil
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * */

#ifndef IIO_TESTS_COMMON_H
#define IIO_TESTS_COMMON_H

#include <getopt.h>

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

struct iio_context * autodetect_context(bool rtn, const char *name, const char *scan);
unsigned long int sanitize_clamp(const char *name, const char *argv,
	uint64_t min, uint64_t max);

/* optstring is a string containing the legitimate option characters.
 * If such a character is followed by a colon, the option  requires  an  argument.
 * Two colons mean an option takes an optional argument.
 */
#define COMMON_OPTIONS "hn:x:u:a::S::T:"

struct iio_context * handle_common_opts(char * name, int argc,
	char * const argv[], const char *optstring,
	const struct option *options, const char *options_descriptions[]);
struct option * add_common_options(const struct option * longopts);
void usage(char *name, const struct option *options, const char *options_descriptions[]);

char ** dup_argv(char * name, int argc, char * argv[]);
void free_argw(int argc, char * argw[]);

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
