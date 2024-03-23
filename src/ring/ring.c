#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include "ring.h"

static char recv_bufs[BUF_COUNT][BUF_SIZE];

static struct io_uring_sqe *get_sqe(struct io_uring *ring)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (!sqe) { fprintf(stderr, "SQ full\n"); exit(1); }
    return sqe;
}

char *ring_get_recv_buf(int bid)
{
    return recv_bufs[bid];
}

void ring_reprovide_buffer(struct io_uring *ring, int bid)
{
    struct io_uring_sqe *sqe = get_sqe(ring);
    io_uring_prep_provide_buffers(sqe, recv_bufs[bid], BUF_SIZE, 1, BGID, bid);
    sqe->user_data = PROV_BUF_SENTINEL;
}

void ring_init(struct io_uring *ring)
{
    int ret = io_uring_queue_init(QUEUE_DEPTH, ring, 0);
    if (ret < 0) {
        /* liburing returns -errno directly on arm64, not -1+errno */
        fprintf(stderr, "io_uring_queue_init: %s\n"
                        "  (Docker Desktop needs --security-opt seccomp=unconfined)\n",
                strerror(-ret));
        exit(1);
    }

    struct io_uring_probe *probe = io_uring_get_probe_ring(ring);
    if (!probe) {
        fprintf(stderr, "io_uring_get_probe_ring failed\n");
        exit(1);
    }
    if (!io_uring_opcode_supported(probe, IORING_OP_SPLICE)) {
        fprintf(stderr, "IORING_OP_SPLICE not supported (need Linux 5.7+)\n");
        free(probe); exit(1);
    }
    if (!io_uring_opcode_supported(probe, IORING_OP_PROVIDE_BUFFERS)) {
        fprintf(stderr, "IORING_OP_PROVIDE_BUFFERS not supported (need Linux 5.7+)\n");
        free(probe); exit(1);
    }
    free(probe);

    struct io_uring_sqe *sqe = get_sqe(ring);
    io_uring_prep_provide_buffers(sqe, recv_bufs, BUF_SIZE, BUF_COUNT, BGID, 0);
    sqe->user_data = PROV_BUF_SENTINEL;
    io_uring_submit(ring);

    struct io_uring_cqe *cqe;
    io_uring_wait_cqe(ring, &cqe);
    if (cqe->res < 0) {
        fprintf(stderr, "provide_buffers: %s\n", strerror(-cqe->res));
        exit(1);
    }
    io_uring_cqe_seen(ring, cqe);
}

void ring_submit_accept(struct io_uring *ring, int listen_fd)
{
    struct io_uring_sqe *sqe = get_sqe(ring);
    io_uring_prep_accept(sqe, listen_fd, NULL, NULL, 0);
    sqe->user_data = ACCEPT_SENTINEL;
}

void ring_submit_recv(struct io_uring *ring, conn_t *c)
{
    struct io_uring_sqe *sqe = get_sqe(ring);
    io_uring_prep_recv(sqe, c->sock_fd, NULL, BUF_SIZE, 0);
    /* buf_group must be set after prep_recv — prep zeroes sqe fields */
    sqe->flags    |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = BGID;
    io_uring_sqe_set_data(sqe, c);
    c->state = STATE_RECV;
}
