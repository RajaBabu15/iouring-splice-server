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
