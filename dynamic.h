/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2022 Analog Devices, Inc.
 * Author: Paul Cercueil
 */

#ifndef __IIO_DYNAMIC_H
#define __IIO_DYNAMIC_H

void * iio_dlopen(const char *path);
void iio_dlclose(void *lib);
void * iio_dlsym(void *lib, const char *symbol);

#endif /* __IIO_DYNAMIC_H */
