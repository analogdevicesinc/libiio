PREFIX ?= /usr/local

COMPILER ?= gcc
CC := $(CROSS_COMPILE)$(COMPILER)
ANALYZER := clang --analyze

IIO_INFO = iio_info
IIO_GENXML = iio_genxml

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

all: $(IIO_INFO) $(IIO_GENXML)

$(IIO_INFO): iio_info.o
	$(SUM) "  LD      $@"
	$(CMD)$(CC) -o $@ $^ $(LDFLAGS)

$(IIO_GENXML): iio_genxml.o
	$(SUM) "  LD      $@"
	$(CMD)$(CC) -o $@ $^ $(LDFLAGS)

clean:
	$(SUM) "  CLEAN   ."
	$(CMD)rm -f $(IIO_INFO) $(IIO_GENXML) \
		iio_info.o iio_genxml.o $(OBJS:%.o=%.plist)

%.o: %.c
	$(SUM) "  CC      $@"
	$(CMD)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
