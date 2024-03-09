CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -g -Isrc
LDFLAGS = -luring
TARGET  = server

SRCS = src/main.c

.PHONY: all build clean

all: build

build: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)
