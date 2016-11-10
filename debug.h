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

#ifdef WITH_COLOR_DEBUG
#ifndef COLOR_DEBUG
#define COLOR_DEBUG   "\e[0;34m"
#endif
#ifndef COLOR_WARNING
#define COLOR_WARNING "\e[01;35m"
#endif
#ifndef COLOR_ERROR
#define COLOR_ERROR   "\e[01;31m"
#endif

#define COLOR_END "\e[0m"
#endif

#if (LOG_LEVEL >= Debug_L)
# ifdef COLOR_DEBUG
#  define DEBUG(str, ...) \
    fprintf(stdout, COLOR_DEBUG "DEBUG: " str COLOR_END, ##__VA_ARGS__)
# else
#  define DEBUG(...) \
    fprintf(stdout, "DEBUG: " __VA_ARGS__)
# endif
#else
#define DEBUG(...) do { } while (0)
#endif

#if (LOG_LEVEL >= Info_L)
# ifdef COLOR_INFO
#  define INFO(str, ...) \
    fprintf(stdout, COLOR_INFO str COLOR_END, ##__VA_ARGS__)
# else
#  define INFO(...) \
    fprintf(stdout, __VA_ARGS__)
# endif
#else
#define INFO(...) do { } while (0)
#endif

#if (LOG_LEVEL >= Warning_L)
# ifdef COLOR_WARNING
#  define WARNING(str, ...) \
    fprintf(stderr, COLOR_WARNING "WARNING: " str COLOR_END, ##__VA_ARGS__)
# else
#  define WARNING(...) \
    fprintf(stderr, "WARNING: " __VA_ARGS__)
# endif
#else
#define WARNING(...) do { } while (0)
#endif

#if (LOG_LEVEL >= Error_L)
# ifdef COLOR_ERROR
#  define ERROR(str, ...) \
    fprintf(stderr, COLOR_ERROR "ERROR: " str COLOR_END, ##__VA_ARGS__)
# else
#  define ERROR(...) \
    fprintf(stderr, "ERROR: " __VA_ARGS__)
# endif
#else
#define ERROR(...) do { } while (0)
#endif

#endif
