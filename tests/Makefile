# libiio - Library for interfacing industrial I/O (IIO) devices
#
# Copyright (C) 2014 Analog Devices, Inc.
# Author: Paul Cercueil <paul.cercueil@analog.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.

PREFIX ?= /usr/local

COMPILER ?= gcc
CC := $(CROSS_COMPILE)$(COMPILER)
ANALYZER := clang --analyze

IIO_INFO = iio_info
IIO_GENXML = iio_genxml
IIO_READDEV = iio_readdev

TARGETS := $(IIO_INFO) $(IIO_GENXML) $(IIO_READDEV)

CFLAGS = -Wall
LDFLAGS = $(wildcard ../libiio.so.*)

ifdef DEBUG
	CFLAGS += -g -ggdb
	CPPFLAGS += -DLOG_LEVEL=4 #-DWITH_COLOR_DEBUG
else
	CFLAGS += -O2
endif

ifdef V
	CMD:=
	SUM:=@\#
else
	CMD:=@
	SUM:=@echo
endif

.PHONY: all clean

all: $(TARGETS)

$(IIO_INFO): iio_info.o
	$(SUM) "  LD      $@"
	$(CMD)$(CC) -o $@ $^ $(LDFLAGS)

$(IIO_GENXML): iio_genxml.o
	$(SUM) "  LD      $@"
	$(CMD)$(CC) -o $@ $^ $(LDFLAGS)

$(IIO_READDEV): iio_readdev.o
	$(SUM) "  LD      $@"
	$(CMD)$(CC) -o $@ $^ $(LDFLAGS)

clean:
	$(SUM) "  CLEAN   ."
	$(CMD)rm -f $(TARGETS) $(TARGETS:%=%.o) $(TARGETS:%=%.plist)

%.o: %.c
	$(SUM) "  CC      $@"
	$(CMD)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
