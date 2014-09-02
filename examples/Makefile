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

include ../makefile.inc

IIO_MONITOR = iio-monitor

TARGETS := ad9361-iiostream $(IIO_MONITOR)

CPPFLAGS = -I..
CFLAGS = -Wall
LDFLAGS = $(wildcard ../libiio.so.*)

.PHONY: all clean

all: $(TARGETS)

$(IIO_MONITOR): iio-monitor.o
	$(SUM) "  LD      $@"
	$(CMD)$(CC) -o $@ $^ $(LDFLAGS) -lpthread -lncurses -lcdk

ad9361-iiostream : ad9361-iiostream.o
	$(SUM) "  LD      $@"
	$(CMD)$(CC) -o $@ $^ $(LDFLAGS)

clean:
	$(SUM) "  CLEAN   ."
	$(CMD)rm -f $(TARGETS) $(TARGETS:%=%.o) $(TARGETS:%=%.plist)
