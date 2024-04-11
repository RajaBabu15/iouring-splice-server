CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -g -Isrc
LDFLAGS = -luring
TARGET  = server

SRCS = src/main.c \
       src/conn/conn.c \
       src/http/http.c \
       src/ring/ring.c \
       src/server/server.c

.PHONY: all build clean

all: build

build: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)
