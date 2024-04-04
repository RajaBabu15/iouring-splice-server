#pragma once
#include <liburing.h>
#include "conn/conn.h"

#define QUEUE_DEPTH       256
#define BUF_SIZE          4096
#define BUF_COUNT         64
#define BGID              0
#define ACCEPT_SENTINEL   1ULL
#define PROV_BUF_SENTINEL 2ULL

#ifndef IORING_CQE_BUFFER_SHIFT
#define IORING_CQE_BUFFER_SHIFT 16
#endif

void  ring_init(struct io_uring *ring);

char *ring_get_recv_buf(int bid);
void  ring_reprovide_buffer(struct io_uring *ring, int bid);

void  ring_submit_accept(struct io_uring *ring, int listen_fd);
void  ring_submit_recv(struct io_uring *ring, conn_t *c);
void  ring_submit_openat(struct io_uring *ring, conn_t *c, int www_dirfd);
void  ring_submit_send_headers(struct io_uring *ring, conn_t *c);
void  ring_submit_send_404(struct io_uring *ring, conn_t *c);
