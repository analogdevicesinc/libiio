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

PREFIX ?= /usr

WITH_LOCAL_BACKEND ?= yes
WITH_NETWORK_BACKEND ?= yes
WITH_AVAHI ?= yes
WITH_PTHREAD ?= yes

VERSION_MAJOR = 0
VERSION_MINOR = 2
VERSION_GIT := $(shell git rev-parse --short HEAD)

COMPILER ?= gcc
CC := $(CROSS_COMPILE)$(COMPILER)
ANALYZER := clang --analyze
INSTALL ?= install
SYSROOT := $(shell $(CC) --print-sysroot)

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

%.o: %.c
	$(SUM) "  CC      $@"
	$(CMD)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

%.c: %.l
	$(SUM) "  LEX     $@"
	$(CMD)flex -o $@ $<

%.c: %.y
	$(SUM) "  YACC    $@"
	$(CMD)yacc --defines=$(@:%.c=%.h) --output=$@ $<
