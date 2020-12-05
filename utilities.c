/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * */

/* Force the XSI version of strerror_r */
#undef _GNU_SOURCE

#include "iio-config.h"
#include "iio-private.h"
#include "network.h"

#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || \
		(defined(__APPLE__) && defined(__MACH__)) || \
		(defined(__USE_XOPEN2K8) && \
		(!defined(__UCLIBC__) || defined(__UCLIBC_HAS_LOCALE__)))
#define LOCALE_SUPPORT
#endif

#ifdef LOCALE_SUPPORT
#if defined(__MINGW32__) || (!defined(_WIN32) && !defined(HAS_NEWLOCALE))
static int read_double_locale(const char *str, double *val)
{
	char *end, *old_locale;
	double value;
	bool problem = false;

	/* XXX: This is not thread-safe, but it's the only way we have to
	 * support locales under MinGW without linking with Visual Studio
	 * libraries. */
	old_locale = iio_strdup(setlocale(LC_NUMERIC, NULL));
	if (!old_locale)
		return -ENOMEM;

	setlocale(LC_NUMERIC, "C");

	errno = 0;
	value = strtod(str, &end);
	if (end == str || errno == ERANGE)
		problem = true;

	setlocale(LC_NUMERIC, old_locale);
	free(old_locale);

	if (problem)
		return -EINVAL;

	*val = value;
	return 0;
}

static int write_double_locale(char *buf, size_t len, double val)
{
	/* XXX: Not thread-safe, see above */
	char *old_locale = iio_strdup(setlocale(LC_NUMERIC, NULL));
	if (!old_locale)
		return -ENOMEM;

	setlocale(LC_NUMERIC, "C");
	iio_snprintf(buf, len, "%f", val);
	setlocale(LC_NUMERIC, old_locale);
	free(old_locale);
	return 0;
}
#elif defined(_WIN32)
static int read_double_locale(const char *str, double *val)
{
	char *end;
	double value;
	bool problem = false;

	_locale_t locale = _create_locale(LC_NUMERIC, "C");
	if (!locale)
		return -ENOMEM;

	errno = 0;
	value = _strtod_l(str, &end, locale);
	if (end == str || errno == ERANGE)
		problem = true;

	_free_locale(locale);

	if (problem)
		return -EINVAL;

	*val = value;
	return 0;
}

static int write_double_locale(char *buf, size_t len, double val)
{
	_locale_t locale = _create_locale(LC_NUMERIC, "C");
	if (!locale)
		return -ENOMEM;

	_snprintf_s_l(buf, len, _TRUNCATE, "%f", locale, val);
	_free_locale(locale);
	return 0;
}
#else
static int read_double_locale(const char *str, double *val)
{
	char *end;
	double value;
	bool problem = false;
	locale_t old_locale, new_locale;

	new_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t) 0);
	if (!new_locale)
		return -errno;

	old_locale = uselocale(new_locale);

	errno = 0;
	value = strtod(str, &end);
	if (end == str || errno == ERANGE)
		problem = true;

	uselocale(old_locale);
	freelocale(new_locale);

	if (problem)
		return -EINVAL;

	*val = value;
	return 0;
}

static int write_double_locale(char *buf, size_t len, double val)
{
	locale_t old_locale, new_locale;

	new_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t) 0);
	if (!new_locale)
		return -errno;

	old_locale = uselocale(new_locale);

	iio_snprintf(buf, len, "%f", val);

	uselocale(old_locale);
	freelocale(new_locale);
	return 0;
}
#endif
#endif

int read_double(const char *str, double *val)
{
#ifdef LOCALE_SUPPORT
	return read_double_locale(str, val);
#else
	char *end;
	double value;

	errno = 0;
	value = strtod(str, &end);
	if (end == str || errno == ERANGE)
		return -EINVAL;

	*val = value;
	return 0;
#endif
}

int write_double(char *buf, size_t len, double val)
{
#ifdef LOCALE_SUPPORT
	return write_double_locale(buf, len, val);
#else
	iio_snprintf(buf, len, "%f", val);
	return 0;
#endif
}

void iio_library_get_version(unsigned int *major,
		unsigned int *minor, char git_tag[8])
{
	if (major)
		*major = LIBIIO_VERSION_MAJOR;
	if (minor)
		*minor = LIBIIO_VERSION_MINOR;
	if (git_tag) {
		iio_strlcpy(git_tag, LIBIIO_VERSION_GIT, 8);
	}
}

void iio_strerror(int err, char *buf, size_t len)
{
#if defined(_WIN32)
	int ret = strerror_s(buf, len, err);
#elif defined(HAS_STRERROR_R)
	int ret = strerror_r(err, buf, len);
#else
	/* no strerror_s, no strerror_r... just use the default message */
	int ret = 0xf7de;
#endif
	if (ret != 0)
		iio_snprintf(buf, len, "Unknown error %i", err);
	else {
		size_t i = strnlen(buf, len);
		iio_snprintf(buf + i, len - i, " (%i)", err);
	}
}

char *iio_strdup(const char *str)
{
#if defined(_WIN32)
	return _strdup(str);
#elif defined(HAS_STRDUP)
	return strdup(str);
#else
	size_t len = strlen(str);
	char *buf = malloc(len + 1);

	if (buf)
		memcpy(buf, str, len + 1);
	return buf;
#endif
}

/* strlcpy is designed to be safer, more consistent, and less error prone
 * replacements for strncpy, since it guarantees to NUL-terminate the result.
 *
 * This function
 * Copyright (c) 1998, 2015 Todd C. Miller <Todd.Miller@courtesan.com>
 * https://github.com/freebsd/freebsd/blob/master/sys/libkern/strlcpy.c
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * Copy string src to buffer dst of size dsize.  At most dsize-1
 * chars will be copied.  Always NUL terminates (unless dsize == 0).
 * Returns strlen(src); if retval >= dsize, truncation occurred.
 *
 * src is assumed to be null terminated, if it is not, this function will
 * dereference unknown memory beyond src.
 */
size_t iio_strlcpy(char * __restrict dst, const char * __restrict src, size_t dsize)
{
	const char *osrc = src;
	size_t nleft = dsize;

	/* Copy as many bytes as will fit. */
	if (nleft != 0) {
		while (--nleft != 0) {
			if ((*dst++ = *src++) == '\0')
				break;
		}
	}

	/* Not enough room in dst, add NUL and traverse rest of src. */
	if (nleft == 0) {
		if (dsize != 0)
			*dst = '\0';            /* NUL-terminate dst */

		while (*src++)
			;
	}

	return(src - osrc - 1); /* count does not include NUL */
}

/* Cross platform version of getenv */

char * iio_getenv (char * envvar)
{
	char *hostname;
	size_t len, tmp;

#ifdef _MSC_BUILD
	if (_dupenv_s(&hostname, NULL, envvar))
		return NULL;
#else
	/* This is qualified below, and a copy is returned
	 * so it's safe to use
	 */
	hostname = getenv(envvar); /* Flawfinder: ignore */
#endif

	if (!hostname)
		return NULL;

	tmp = MAXHOSTNAMELEN + sizeof("serial:") + sizeof(":65535") - 2;
	len = strnlen(hostname, tmp);

	/* Should be smaller than max length */
	if (len == tmp)
		goto wrong_str;

	/* should be more than "usb:" or "ip:" */
	tmp = sizeof("ip:") - 1;
	if (len < tmp)
		goto wrong_str;

#ifdef _MSC_BUILD
	return hostname;
#else
	return strdup (hostname);
#endif

wrong_str:
#ifdef _WIN32
	free(hostname);
#endif
	return NULL;
}
