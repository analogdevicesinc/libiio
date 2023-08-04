/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#ifndef DEBUG_H
#define DEBUG_H

#include "iio-config.h"

#include <stdio.h>

#define NoLog_L 0
#define Error_L 1
#define Warning_L 2
#define Info_L 3
#define Debug_L 4

/* -------------------- */

/* Many of these debug printf include a Flawfinder: ignore, this is because,
 * according to https://cwe.mitre.org/data/definitions/134.html which describes
 * functions that accepts a format string as an argument, but the format
 * string originates from an external source. All the IIO_DEBUG, IIO_INFO,
 * IIO_WARNING, and IIO_ERRRO functions are called internally from the
 * library, have fixed format strings and can not be modified externally.
 */
#define IIO_DEBUG(...) \
    do { \
	if (LOG_LEVEL >= Debug_L) \
            fprintf(stdout, "DEBUG: " __VA_ARGS__); /* Flawfinder: ignore */ \
    } while (0)

#define IIO_INFO(...) \
    do { \
	if (LOG_LEVEL >= Info_L) \
            fprintf(stdout, __VA_ARGS__); /* Flawfinder: ignore */ \
    } while (0)

#define IIO_WARNING(...) \
    do { \
	if (LOG_LEVEL >= Warning_L) \
            fprintf(stderr, "WARNING: " __VA_ARGS__); /* Flawfinder: ignore */ \
    } while (0)

#define IIO_ERROR(...) \
    do { \
	if (LOG_LEVEL >= Error_L) \
            fprintf(stderr, "ERROR: " __VA_ARGS__); /* Flawfinder: ignore */ \
    } while (0)

#endif
