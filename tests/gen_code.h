/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014, 2019 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *         Robin Getz <robin.getz@analog.com>
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

#ifndef GEN_CODE_H
#define GEN_CODE_H

void gen_start(const char *gen_file);
bool gen_test_path(const char *gen_file);
void gen_context (const char *uri);
void gen_context_destroy();
void gen_context_attr(const char *key);
void gen_dev(const struct iio_device *dev);
void gen_ch(const struct iio_channel *ch);
void gen_function(const char* prefix, const char* target,
                const char* attr, const char* wbuf);
void gen_context_timeout(unsigned int timeout_ms);
#endif
