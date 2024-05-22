# ── Build stage ──────────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc \
    make \
    liburing-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY src/   ./src/
COPY Makefile .

RUN make build

# ── Runtime stage ─────────────────────────────────────────────────────────────
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    liburing2 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /build/server .

# Default www content (override with: -v $(pwd)/www:/app/www)
RUN mkdir -p www && \
    printf '<!DOCTYPE html><html><body><h1>io_uring splice server</h1><p>Zero-copy via IORING_OP_SPLICE</p></body></html>\n' > www/index.html && \
    printf 'Hello!\n' > www/hello.txt

EXPOSE 8080
CMD ["./server", "8080"]
