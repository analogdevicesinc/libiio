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

.PHONY: all clean analyze install install-lib uninstall uninstall-lib

$(LIBIIO): $(OBJS)
	$(SUM) "  LD      $@"
	$(CMD)$(CC) -shared -Wl,-soname,$(SONAME) -o $@ $^ $(LDFLAGS) $(CFLAGS)

all: $(LIBIIO) test

clean:
	$(SUM) "  CLEAN   ."
	$(CMD)rm -f $(LIBIIO) $(OBJS) $(OBJS:%.o=%.plist) test.o test

analyze:
	$(ANALYZER) $(CFLAGS) $(OBJS:%.o=%.c)

test: test.o $(LIBIIO)
	$(SUM) "  LD      $@"
	$(CMD)$(CC) -o $@ $^ $(LDFLAGS)

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
