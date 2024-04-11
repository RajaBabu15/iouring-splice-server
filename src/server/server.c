#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "server.h"
#include "conn/conn.h"
#include "http/http.h"
#include "ring/ring.h"

static int s_listen_fd;
static int s_www_dirfd;
static struct io_uring *s_ring;

static void handle_accept(struct io_uring_cqe *cqe)
{
    if (cqe->res < 0) {
        if (cqe->res != -EINTR)
            fprintf(stderr, "accept: %s\n", strerror(-cqe->res));
        ring_submit_accept(s_ring, s_listen_fd);
        return;
    }
    conn_t *c = conn_alloc();
    if (!c) {
        close(cqe->res);
        ring_submit_accept(s_ring, s_listen_fd);
        return;
    }
    c->sock_fd = cqe->res;
    ring_submit_recv(s_ring, c);
    ring_submit_accept(s_ring, s_listen_fd);    /* keep one accept always in flight */
}

static void handle_recv(conn_t *c, struct io_uring_cqe *cqe)
{
    if (cqe->res == -ENOBUFS) { conn_free(c); return; }
    if (cqe->res <= 0)         { conn_free(c); return; }

    /* Buffer ID is only valid when the kernel set IORING_CQE_F_BUFFER */
    if (!(cqe->flags & IORING_CQE_F_BUFFER)) { conn_free(c); return; }

    int bid = (int)(cqe->flags >> IORING_CQE_BUFFER_SHIFT);

    char url_path[256];
    char *buf = ring_get_recv_buf(bid);
    if (http_parse_request(buf, cqe->res, url_path, sizeof(url_path)) < 0) {
        c->hdrlen = (size_t)http_build_404_response(c->hdrbuf, sizeof(c->hdrbuf));
        ring_submit_send_404(s_ring, c);
        return;
    }
    const char *relpath = http_normalize_path(url_path);
    if (!relpath) {
        c->hdrlen = (size_t)http_build_404_response(c->hdrbuf, sizeof(c->hdrbuf));
        ring_submit_send_404(s_ring, c);
        return;
    }
    strncpy(c->path, relpath, sizeof(c->path) - 1);
    c->path[sizeof(c->path) - 1] = '\0';
    ring_reprovide_buffer(s_ring, bid);
    ring_submit_openat(s_ring, c, s_www_dirfd);
}

int server_setup_listen_socket(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons((uint16_t)port),
        .sin_addr.s_addr = INADDR_ANY,
    };
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(fd, SOMAXCONN) < 0) {
        perror("listen"); exit(1);
    }
    return fd;
}

int server_open_www_dir(const char *path)
{
    int fd = open(path, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        perror(path);
        fprintf(stderr, "run 'make www' first\n");
        exit(1);
    }
    return fd;
}

void server_event_loop(struct io_uring *ring, int listen_fd, int www_dirfd)
{
    s_ring      = ring;
    s_listen_fd = listen_fd;
    s_www_dirfd = www_dirfd;

    ring_submit_accept(ring, listen_fd);
    io_uring_submit(ring);

    for (;;) {
        int ret = io_uring_submit_and_wait(ring, 1);
        if (ret < 0 && ret != -EINTR) {
            fprintf(stderr, "io_uring_submit_and_wait: %s\n", strerror(-ret));
            exit(1);
        }

        unsigned int head, count = 0;
        struct io_uring_cqe *cqe;

        io_uring_for_each_cqe(ring, head, cqe) {
            count++;
            uint64_t ud = cqe->user_data;

            if (ud == ACCEPT_SENTINEL) {
                handle_accept(cqe);
            } else if (ud == PROV_BUF_SENTINEL) {
                if (cqe->res < 0)
                    fprintf(stderr, "provide_buffers: %s\n", strerror(-cqe->res));
            } else {
                conn_t *c = (conn_t *)(uintptr_t)ud;
                switch (c->state) {
                case STATE_RECV: handle_recv(c, cqe); break;
                default:
                    fprintf(stderr, "unknown state %d\n", c->state);
                    conn_free(c);
                    break;
                }
            }
        }
        io_uring_cq_advance(ring, count);
    }
}
