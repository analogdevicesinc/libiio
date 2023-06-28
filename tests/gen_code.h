/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014, 2019 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *         Robin Getz <robin.getz@analog.com>
 */

#ifndef GEN_CODE_H
#define GEN_CODE_H

void gen_start(const char *gen_file);
bool gen_test_path(const char *gen_file);
void gen_context (const char *uri);
void gen_context_destroy(void);
void gen_context_attr(const char *key);
void gen_dev(const struct iio_device *dev);
void gen_ch(const struct iio_channel *ch);
void gen_function(const char* prefix, const char* target,
                const char* attr, const char* wbuf);
void gen_context_timeout(unsigned int timeout_ms);
#endif
