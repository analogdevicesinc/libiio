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

VERSION_MAJOR = 0
VERSION_MINOR = 1

LIBNAME = libiio.so
SONAME = $(LIBNAME).$(VERSION_MAJOR)
LIBIIO = $(SONAME).$(VERSION_MINOR)

COMPILER ?= gcc
CC := $(CROSS_COMPILE)$(COMPILER)
ANALYZER := clang --analyze
INSTALL ?= install

# XXX: xml2-config is not sysroot aware...
SYSROOT := $(shell $(CC) --print-sysroot)
XML2_CFLAGS := $(shell $(SYSROOT)/usr/bin/xml2-config --cflags)
XML2_LIBS := $(shell $(SYSROOT)/usr/bin/xml2-config --libs)

CFLAGS := $(XML2_CFLAGS) -Wall -fPIC -fvisibility=hidden
LDFLAGS := $(XML2_LIBS) -lsysfs

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

OBJS := context.o device.o channel.o local.o xml.o

.PHONY: all clean tests analyze install install-lib uninstall uninstall-lib

$(LIBIIO): $(OBJS)
	$(SUM) "  LD      $@"
	$(CMD)$(CC) -shared -Wl,-soname,$(SONAME) -o $@ $^ $(LDFLAGS) $(CFLAGS)

all: $(LIBIIO) tests

clean-tests:
	$(CMD)$(MAKE) -C tests clean

clean: clean-tests
	$(SUM) "  CLEAN   ."
	$(CMD)rm -f $(LIBIIO) $(OBJS) $(OBJS:%.o=%.plist)

analyze:
	$(ANALYZER) $(CFLAGS) $(OBJS:%.o=%.c)

tests: $(LIBIIO)
	$(CMD)$(MAKE) -C tests

%.o: %.c
	$(SUM) "  CC      $@"
	$(CMD)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

install-lib: $(LIBIIO)
	$(INSTALL) -D $(LIBIIO) $(DESTDIR)$(PREFIX)/lib/$(LIBIIO)
	ln -sf $(LIBIIO) $(DESTDIR)$(PREFIX)/lib/$(SONAME)

install: install-lib
	$(INSTALL) -D -m 0644 iio.h $(DESTDIR)$(PREFIX)/include/iio.h
	ln -sf $(SONAME) $(DESTDIR)$(PREFIX)/lib/$(LIBNAME)

uninstall-lib:
	rm -f $(DESTDIR)$(PREFIX)/lib/$(LIBIIO) $(DESTDIR)$(PREFIX)/lib/$(SONAME)

uninstall: uninstall-lib
	rm -f $(DESTDIR)$(PREFIX)/include/iio.h $(DESTDIR)$(PREFIX)/lib/$(LIBNAME)
