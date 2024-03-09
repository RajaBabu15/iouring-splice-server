# io_uring splice server

HTTP/1.1 static file server built on Linux `io_uring`. The goal is to serve
static files with zero userspace copies by using `IORING_OP_SPLICE` to move file
data through a pipe straight to the socket.

## Requirements

Linux 5.7+ with `liburing`:

```bash
sudo apt install liburing-dev gcc make
```

## Build

```bash
make build
```

Work in progress.
