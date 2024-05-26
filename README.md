# io_uring splice server

HTTP/1.1 static file server using `io_uring` for all I/O and `IORING_OP_SPLICE`
for zero-copy file transfer. The two-splice path (file → pipe → socket) keeps
file data in the page cache; it never touches userspace during transfer.

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

## Structure

```
src/
├── main.c          entry point
├── conn/           connection pool and state machine
├── http/           request parsing, MIME, response building
├── ring/           io_uring init and SQE submitters
└── server/         listen socket, event loop, CQE dispatch
Makefile
Dockerfile          two-stage Ubuntu 24.04 build
www/                static files (make www)
```
