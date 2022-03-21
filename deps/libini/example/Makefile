

TARGET = test

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -O2
LDFLAGS = -lini

OBJS = test.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

clean:
	rm -f $(OBJS) $(TARGET)
