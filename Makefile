CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -g -Isrc
LDFLAGS = -luring
TARGET  = server

SRCS = src/main.c \
       src/conn/conn.c \
       src/http/http.c \
       src/ring/ring.c \
       src/server/server.c

.PHONY: all build run www bench clean docker-build docker-run

all: build

build: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Create sample files for the server to serve
www:
	@mkdir -p www
	@printf '<!DOCTYPE html><html><body>\n<h1>io_uring splice server</h1>\n<p>Zero-copy file serving via <code>IORING_OP_SPLICE</code>: file &rarr; pipe &rarr; socket.</p>\n</body></html>\n' > www/index.html
	@printf 'Hello from io_uring splice server!\n' > www/hello.txt
	@dd if=/dev/urandom of=www/big.bin    bs=1048576 count=4  2>/dev/null && echo "Created www/big.bin    (4 MB)"
	@dd if=/dev/urandom of=www/medium.bin bs=65536   count=1  2>/dev/null && echo "Created www/medium.bin (64 KB — exactly one pipe capacity)"
	@echo "www/ ready."

run: build www
	./$(TARGET) $(or $(PORT),8080)

# Requires wrk: brew install wrk (macOS) / apt install wrk (Linux)
bench:
	@which wrk > /dev/null 2>&1 || { echo "wrk not found. Install: brew install wrk"; exit 1; }
	@echo "=== small file (index.html) ==="
	wrk -t4 -c100 -d15s http://localhost:$(or $(PORT),8080)/index.html
	@echo "\n=== large file (big.bin, 4 MB) ==="
	wrk -t4 -c50  -d15s http://localhost:$(or $(PORT),8080)/big.bin

docker-build:
	docker build -t iouring-splice-server .

docker-run: docker-build www
	docker run --rm -p $(or $(PORT),8080):8080 \
	    --security-opt seccomp=unconfined \
	    -v "$(CURDIR)/www:/app/www" \
	    iouring-splice-server

clean:
	rm -f $(TARGET)
