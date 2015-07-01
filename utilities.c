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

#include "iio-private.h"

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || (defined(__USE_XOPEN2K8) && \
		(!defined(__UCLIBC__) || defined(__UCLIBC_HAS_LOCALE__)))
#define LOCALE_SUPPORT
#endif

#ifdef LOCALE_SUPPORT
#if defined(__MINGW32__)
static int read_double_locale(const char *str, double *val)
{
	char *end, *old_locale;
	double value;

	/* XXX: This is not thread-safe, but it's the only way we have to
	 * support locales under MinGW without linking with Visual Studio
	 * libraries. */
	old_locale = strdup(setlocale(LC_NUMERIC, NULL));
	if (!old_locale)
		return -ENOMEM;

	setlocale(LC_NUMERIC, "C");
	value = strtod(str, &end);
	setlocale(LC_NUMERIC, old_locale);
	free(old_locale);

	if (end == str)
		return -EINVAL;

	*val = value;
	return 0;
}

static int write_double_locale(char *buf, size_t len, double val)
{
	/* XXX: Not thread-safe, see above */
	char *old_locale = strdup(setlocale(LC_NUMERIC, NULL));
	if (!old_locale)
		return -ENOMEM;

	setlocale(LC_NUMERIC, "C");
	snprintf(buf, len, "%f", val);
	setlocale(LC_NUMERIC, old_locale);
	free(old_locale);
	return 0;
}
#elif defined(_WIN32)
static int read_double_locale(const char *str, double *val)
{
	char *end;
	double value;
	_locale_t locale = _create_locale(LC_NUMERIC, "C");
	if (!locale)
		return -ENOMEM;

	value = _strtod_l(str, &end, locale);
	_free_locale(locale);

	if (end == str)
		return -EINVAL;

	*val = value;
	return 0;
}

static int write_double_locale(char *buf, size_t len, double val)
{
	_locale_t locale = _create_locale(LC_NUMERIC, "C");
	if (!locale)
		return -ENOMEM;

	_snprintf_l(buf, len, "%f", locale, val);
	_free_locale(locale);
	return 0;
}
#else
static int read_double_locale(const char *str, double *val)
{
	char *end;
	double value;
	locale_t old_locale, new_locale;

	new_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t) 0);
	if (!new_locale)
		return -errno;

	old_locale = uselocale(new_locale);

	value = strtod(str, &end);
	uselocale(old_locale);
	freelocale(new_locale);

	if (end == str)
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

	snprintf(buf, len, "%f", val);

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
	double value = strtod(str, &end);

	if (end == str)
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
	snprintf(buf, len, "%f", val);
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
		strncpy(git_tag, LIBIIO_VERSION_GIT, 8);
		git_tag[7] = '\0';
	}
}

void iio_strerror(int err, char *buf, size_t len)
{
#ifdef _WIN32
	int ret = strerror_s(buf, len, err);
	if (ret != 0)
		snprintf(buf, len, "Unknown error %i", err);
#elif (_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600) && ! _GNU_SOURCE
	int ret = strerror_r(err, buf, len);
	if (ret != 0)
		snprintf(buf, len, "Unknown error %i", err);
#else
	char *str = strerror_r(err, buf, len);
	if (str != buf)
		strncpy(buf, str, len);
#endif
}

int set_blocking_mode(int fd, bool blocking)
{
#ifndef _WIN32
	int ret = fcntl(fd, F_GETFL, 0);
	if (ret < 0)
		return -errno;

	if (blocking)
		ret &= ~O_NONBLOCK;
	else
		ret |= O_NONBLOCK;

	ret = fcntl(fd, F_SETFL, ret);
	return ret < 0 ? -errno : 0;
#else
	return -ENOSYS;
#endif
}
