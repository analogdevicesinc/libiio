/* SPDX-license-identifier: LGPL-v2-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#ifndef __IIOD_DNS_SD_H
#define __IIOD_DNS_SD_H

#include <stdint.h>

struct thread_pool;

void start_avahi(struct thread_pool *pool, uint16_t port);
void stop_avahi(void);

#endif /* __IIOD_DNS_SD_H */
