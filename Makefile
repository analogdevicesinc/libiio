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

include makefile.inc

LIBNAME = libiio.so
SONAME = $(LIBNAME).$(VERSION_MAJOR)
LIBIIO = $(SONAME).$(VERSION_MINOR)

# XXX: xml2-config is not sysroot aware...
XML2_CFLAGS := $(shell $(SYSROOT)/usr/bin/xml2-config --cflags)
XML2_LIBS := $(shell $(SYSROOT)/usr/bin/xml2-config --libs)

CPPFLAGS := -D_POSIX_C_SOURCE=200809L \
	-DLIBIIO_VERSION_GIT="\"$(VERSION_GIT)\"" \
	-DLIBIIO_VERSION_MAJOR=$(VERSION_MAJOR) \
	-DLIBIIO_VERSION_MINOR=$(VERSION_MINOR)

CFLAGS := $(XML2_CFLAGS) -Wall -Wextra -fPIC -fvisibility=hidden \
	-std=c99 -pedantic
LDFLAGS := $(XML2_LIBS)

ifeq ($(WITH_AVAHI),yes)
	CPPFLAGS += -DHAVE_AVAHI
	LDFLAGS += -lavahi-client -lavahi-common
endif

ifeq ($(WITH_PTHREAD),yes)
	LDFLAGS += -lpthread
else
	CPPFLAGS += -DHAVE_PTHREAD=0
endif

OBJS := context.o device.o channel.o buffer.o utilities.o

ifeq ($(WITH_LOCAL_BACKEND),yes)
	OBJS += local.o
endif
ifeq ($(WITH_NETWORK_BACKEND),yes)
	OBJS += network.o xml.o
endif

.PHONY: all clean tests examples analyze install \
	install-lib uninstall uninstall-lib html iiod \
	install-sysroot uninstall-sysroot

all: libiio iiod tests

$(LIBIIO): $(OBJS)
	$(SUM) "  LD      $@"
	$(CMD)$(CC) -shared -Wl,-soname,$(SONAME) -o $@ $^ $(LDFLAGS) $(CFLAGS)

libiio: $(LIBIIO) libiio.pc

html: Doxyfile
	$(SUM) "  GEN     $@"
	$(CMD) -doxygen $<
	$(CMD)cp -r doc $@

clean-tests clean-examples clean-iiod:
	$(CMD)$(MAKE) -C $(@:clean-%=%) clean

clean: clean-tests clean-examples clean-iiod
	$(SUM) "  CLEAN   ."
	$(CMD)rm -rf $(LIBIIO) $(OBJS) $(OBJS:%.o=%.plist) libiio.pc html

analyze:
	$(ANALYZER) $(CFLAGS) $(OBJS:%.o=%.c)

tests examples iiod: $(LIBIIO)
	$(CMD)$(MAKE) -C $@

libiio.pc: libiio.pc.in
	$(SUM) "  GEN     $@"
	$(CMD)sed 's/_PREFIX/$(subst /,\/,$(PREFIX))/;s/_VERSION/$(VERSION_MAJOR).$(VERSION_MINOR)/' $< > $@

install-tests install-examples install-iiod:
	$(CMD)PREFIX=$(PREFIX) $(MAKE) -C $(@:install-%=%) install

uninstall-tests uninstall-examples uninstall-iiod:
	$(CMD)PREFIX=$(PREFIX) $(MAKE) -C $(@:uninstall-%=%) uninstall

install-lib: $(LIBIIO)
	$(INSTALL) -D $(LIBIIO) $(DESTDIR)$(PREFIX)/lib/$(LIBIIO)
	ln -sf $(LIBIIO) $(DESTDIR)$(PREFIX)/lib/$(SONAME)

install-sysroot: install-lib libiio.pc
	$(INSTALL) -D -m 0644 iio.h $(DESTDIR)$(PREFIX)/include/iio.h
	ln -sf $(SONAME) $(DESTDIR)$(PREFIX)/lib/$(LIBNAME)
	$(INSTALL) -D -m 0644 libiio.pc $(DESTDIR)$(PREFIX)/lib/pkgconfig/libiio.pc

install: install-lib install-iiod install-tests install-sysroot

uninstall-lib:
	rm -f $(DESTDIR)$(PREFIX)/lib/$(LIBIIO) $(DESTDIR)$(PREFIX)/lib/$(SONAME)

uninstall-sysroot:
	rm -f $(DESTDIR)$(PREFIX)/include/iio.h $(DESTDIR)$(PREFIX)/lib/$(LIBNAME)
	rm -f $(DESTDIR)$(PREFIX)/lib/pkgconfig/libiio.pc

uninstall: uninstall-lib uninstall-iiod uninstall-tests uninstall-sysroot
