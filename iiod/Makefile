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
INSTALL ?= install

DAEMON = iiod

CFLAGS = -Wall
LDFLAGS = $(wildcard ../libiio.so.*) -lpthread

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

OBJS = iiod.o lexer.o ops.o parser.o

.PHONY: all clean install

all: $(DAEMON)

$(DAEMON): $(OBJS)
	$(SUM) "  LD      $@"
	$(CMD)$(CC) -o $@ $^ $(LDFLAGS)

clean:
	$(SUM) "  CLEAN   ."
	$(CMD)rm -f $(DAEMON) $(OBJS) lexer.c parser.h parser.c $(OBJS:%.o=%.plist)

%.o: %.c
	$(SUM) "  CC      $@"
	$(CMD)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

%.c: %.l
	$(SUM) "  LEX     $@"
	$(CMD)flex -o $@ $<

%.c: %.y
	$(SUM) "  YACC    $@"
	$(CMD)yacc --defines=$(@:%.c=%.h) --output=$@ $<

parser.h: parser.c
lexer.o ops.o: parser.h
lexer.o: CFLAGS += -Wno-unused-function

install: $(DAEMON)
	$(INSTALL) -D -m 0755 $(DAEMON) $(DESTDIR)$(PREFIX)/bin/$(DAEMON)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(DAEMON)
