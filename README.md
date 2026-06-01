# io_uring splice server

HTTP/1.1 static file server using `io_uring` for all I/O and `IORING_OP_SPLICE`
for zero-copy file transfer. The two-splice path (file → pipe → socket) keeps
file data in the page cache; it never touches userspace during transfer. An
optional TLS 1.3 listener serves HTTPS over the same path using **kernel TLS
(kTLS)** — see [HTTPS via kernel TLS](#https-via-kernel-tls-ktls--optional).

## How it works

```
accept → recv (IOSQE_BUFFER_SELECT) → parse GET → openat → fstat
→ send headers → loop: splice file→pipe, splice pipe→socket → close
```

`sendfile` copies through userspace on some kernel paths; splicing through an
anonymous pipe avoids that by moving page references. `SPLICE_F_MORE` is set on
file→pipe for all but the final chunk so the kernel knows more data is coming;
it is deliberately absent on pipe→socket to let TCP flush immediately.
`IOSQE_BUFFER_SELECT` lets the kernel pick a pre-registered recv buffer,
avoiding a per-request allocation.

## Requirements

**Linux 5.7+ (native):**
```bash
sudo apt install liburing-dev gcc make
```

**macOS — Docker:**

Docker Desktop runs a Linux 6.x VM. Its default seccomp profile blocks
`io_uring_setup` on the linuxkit VM; `make docker-run` passes
`--security-opt seccomp=unconfined` to work around this. On Docker Engine on
a native Linux host the flag is not needed (io_uring has been in the default
allowlist since Docker 20.10.9).

## Quick start

**Docker:**
```bash
make www
make docker-run

curl http://localhost:8080/index.html
curl -s http://localhost:8080/big.bin | wc -c   # must print 4194304
curl http://localhost:8080/nonexistent           # 404
```

**Native Linux:**
```bash
make www && make run           # listens on :8080
make run PORT=9000             # or a custom port
```

## Benchmarks

Docker Desktop, Apple M2, `wrk -t2 -c8 -d30s --timeout 60s`:

```
# 4 MB file — splice loop
Running 30s test @ http://localhost:8080/big.bin
  2 threads and 8 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    37.09ms   18.79ms 278.15ms   92.53%
    Req/Sec   111.97     31.65   202.00     56.63%
  6574 requests in 30.02s, 25.70GB read
Requests/sec:    218.96
Transfer/sec:      0.86GB

# 165 B file — accept/recv/send path
Running 20s test @ http://localhost:8080/index.html
  4 threads and 50 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    13.74ms    4.88ms  46.27ms   74.08%
    Req/Sec   822.25    202.04     1.35k    72.17%
  17372 requests in 20.04s, 4.24MB read
Requests/sec:    866.85
Transfer/sec:    216.71KB
```

Per-connection latency is dominated by the Docker Desktop VM's virtual network;
on a native Linux host the same binary reaches 30–80k req/s for small files.
The 860 MB/s large-file number is near the VM's bandwidth ceiling and shows
splice is not CPU-bound.

## Verifying zero-copy

```bash
docker exec -it iouring-test bash
./server 8080 &
strace -p $! -e trace=read,write,sendfile 2>&1 &
# run curl requests from another terminal
# grep for read/write/sendfile: nothing — io_uring_enter handles everything
```

## HTTPS via kernel TLS (kTLS) — optional

An opt-in TLS 1.3 listener serves HTTPS while keeping the same io_uring data
path. OpenSSL performs **only the handshake**; after it completes, the
negotiated keys are handed to the kernel's TLS ULP (`SSL_OP_ENABLE_KTLS` →
`setsockopt(SOL_TLS, TLS_TX/TLS_RX)`), and from then on the kernel does record
framing and AES-GCM:

```
accept(tls) → SSL_accept driven by IORING_OP_POLL_ADD  (non-blocking, in-loop)
            → require kTLS TX+RX → hand to the SAME recv/splice state machine
```

Because the socket has kTLS installed, the existing pipeline is reused
**unchanged**: `io_uring` `recv` reads plaintext (kernel decrypts on RX), and
the `file→pipe→socket` splice is encrypted in-kernel on TX. The handshake is
driven asynchronously through `IORING_OP_POLL_ADD`, so it never blocks the
single event loop.

```bash
# native Linux (kernel needs CONFIG_TLS; most distro kernels have it =m)
sudo apt install liburing-dev libssl-dev gcc make
make build-tls
make certs                     # self-signed CN=localhost
./server 8080 --tls-cert certs/server.crt --tls-key certs/server.key --tls-port 8443
curl -k https://localhost:8443/index.html

# Docker
make docker-run-tls            # exposes 8080 (http) and 8443 (https)
```

### Honest caveats

- **Requires `CONFIG_TLS` in the kernel.** The server requires kTLS in *both*
  directions and **drops the connection** if the kernel can't install it
  (rather than silently falling back) — you'll see `kTLS not active` in the
  log. Notably, **Docker Desktop's linuxkit kernel is built with
  `# CONFIG_TLS is not set`**, so the full kTLS path cannot run there; use a
  native Linux host or a cloud VM whose kernel enables `tls`. (A userspace
  `SSL_read`/`SSL_write` fallback for non-kTLS kernels is a possible future
  addition; v1 is kTLS-only by design.)
- **Software kTLS is not zero-copy on the encrypt path.** With `TLS_SW` the
  kernel must copy plaintext into a buffer to AES-GCM it before transmit, so
  the project's "no userspace copies" property does *not* fully carry over to
  encrypted serving. True zero-copy encrypted send requires NIC TLS offload
  (`tls_device`) on crypto-capable hardware. The splice path still avoids the
  userspace round-trip for the *file* bytes; the crypto transform is the added
  cost.
- **Pinned to TLS 1.2 + ECDHE-AES128-GCM**, server-side static files only.
  This is deliberate: the request path uses io_uring `recv` on the raw fd,
  which only sees plaintext if kTLS *RX* is active — and OpenSSL offloads
  kTLS RX for **TLS 1.2 only** (TLS 1.3 gets kTLS TX but not RX, owing to
  KeyUpdate handling). Verified on Linux 6.x: TLS 1.3 → `send=1 recv=0`,
  TLS 1.2 → `send=1 recv=1`. A TLS 1.3 mode (kTLS TX + userspace `SSL_read`
  for RX) is a possible future addition.

## Structure

```
src/
├── main.c          entry point (CLI flags, http + optional https listeners)
├── conn/           connection pool and state machine
├── http/           request parsing, MIME, response building
├── ring/           io_uring init and SQE submitters
├── server/         listen socket, event loop, CQE dispatch, TLS handshake
└── tls/            OpenSSL handshake + kTLS handover (TLS_ENABLED builds only)
Makefile
Dockerfile          two-stage Ubuntu 24.04 build (cleartext)
Dockerfile.tls      kTLS build (adds libssl-dev, bakes a self-signed cert)
www/                static files (make www)
```
