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
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

int read_double(const char *str, double *val)
{
	double value;
	char *end;
#ifdef _WIN32
	int config;
	_locale_t old_locale;

	config = _configurethreadlocale(_ENABLE_PER_THREAD_LOCALE);
	old_locale = _get_current_locale();
	setlocale(LC_NUMERIC, "POSIX");
#else
	locale_t old_locale, new_locale;

	new_locale = newlocale(LC_NUMERIC_MASK, "POSIX", (locale_t) 0);
	old_locale = uselocale(new_locale);
#endif

	value = strtod(str, &end);

#ifdef _WIN32
	setlocale(old_locale);
	_configurethreadlocale(config);
#else
	uselocale(old_locale);
	freelocale(new_locale);
#endif

	if (end == str)
		return -EINVAL;

	*val = value;
	return 0;
}

void write_double(char *buf, size_t len, double val)
{
#ifdef _WIN32
	int config;
	_locale_t old_locale;

	config = _configurethreadlocale(_ENABLE_PER_THREAD_LOCALE);
	old_locale = _get_current_locale();
	setlocale(LC_NUMERIC, "POSIX");
#else
	locale_t old_locale, new_locale;

	new_locale = newlocale(LC_NUMERIC_MASK, "POSIX", (locale_t) 0);
	old_locale = uselocale(new_locale);
#endif

	snprintf(buf, len, "%lf", val);

#ifdef _WIN32
	setlocale(old_locale);
	_configurethreadlocale(config);
#else
	uselocale(old_locale);
	freelocale(new_locale);
#endif
}
