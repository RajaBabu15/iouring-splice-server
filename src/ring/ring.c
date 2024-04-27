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

void ring_submit_openat(struct io_uring *ring, conn_t *c, int www_dirfd)
{
    struct io_uring_sqe *sqe = get_sqe(ring);
    io_uring_prep_openat(sqe, www_dirfd, c->path, O_RDONLY, 0);
    io_uring_sqe_set_data(sqe, c);
    c->state = STATE_OPEN_FILE;
}

void ring_submit_send_headers(struct io_uring *ring, conn_t *c)
{
    struct io_uring_sqe *sqe = get_sqe(ring);
    io_uring_prep_send(sqe, c->sock_fd, c->hdrbuf, c->hdrlen, MSG_NOSIGNAL);
    io_uring_sqe_set_data(sqe, c);
    c->state = STATE_SEND_HEADERS;
}

void ring_submit_send_404(struct io_uring *ring, conn_t *c)
{
    struct io_uring_sqe *sqe = get_sqe(ring);
    io_uring_prep_send(sqe, c->sock_fd, c->hdrbuf, c->hdrlen, MSG_NOSIGNAL);
    io_uring_sqe_set_data(sqe, c);
    c->state = STATE_SEND_404;
}

void ring_submit_splice_file_to_pipe(struct io_uring *ring, conn_t *c)
{
    off_t    remaining = c->file_size - c->file_offset;
    size_t   chunk     = (size_t)((off_t)PIPE_CAPACITY < remaining
                                   ? PIPE_CAPACITY : remaining);
    int      is_last   = ((off_t)chunk >= remaining);
    /* SPLICE_F_MORE on non-final chunks: tells kernel more data follows into
     * the pipe, avoiding a premature wakeup. Omit on the last chunk. */
    unsigned flags     = SPLICE_F_MOVE | (is_last ? 0u : SPLICE_F_MORE);

    struct io_uring_sqe *sqe = get_sqe(ring);
    io_uring_prep_splice(sqe,
        c->file_fd, (int64_t)c->file_offset,
        c->pipe_wr, -1,
        (unsigned int)chunk, flags);
    io_uring_sqe_set_data(sqe, c);
    c->state = STATE_SPLICE_FILE_TO_PIPE;
}

void ring_submit_splice_pipe_to_sock(struct io_uring *ring, conn_t *c)
{
    /* No SPLICE_F_MORE here: would delay TCP push and hold data in sendbuf */
    struct io_uring_sqe *sqe = get_sqe(ring);
    io_uring_prep_splice(sqe,
        c->pipe_rd, -1,
        c->sock_fd, -1,
        (unsigned int)c->bytes_to_send, SPLICE_F_MOVE);
    io_uring_sqe_set_data(sqe, c);
    c->state = STATE_SPLICE_PIPE_TO_SOCK;
}
