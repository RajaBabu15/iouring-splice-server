#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define MAX_CONNECTIONS 1024

typedef enum {
    STATE_RECV,
    STATE_OPEN_FILE,
    STATE_SEND_HEADERS,
    STATE_SPLICE_FILE_TO_PIPE,
    STATE_SPLICE_PIPE_TO_SOCK,
    STATE_SEND_404,
    STATE_TLS_HANDSHAKE,        /* TLS only: SSL_accept driven by io_uring poll */
} conn_state_t;

typedef struct conn {
    conn_state_t  state;
    int           sock_fd;
    int           file_fd;
    int           pipe_rd;
    int           pipe_wr;
    off_t         file_size;
    off_t         file_offset;
    size_t        bytes_to_send;    /* bytes currently sitting in the pipe */
    char          hdrbuf[512];      /* response headers (and 404 body) */
    size_t        hdrlen;
    char          path[256];        /* relative fs path under www/ */
    int           free_next;
    bool          in_use;
    bool          tls;              /* connection arrived on the TLS listener */
    void         *ssl;              /* opaque SSL* (TLS builds only); NULL otherwise */
} conn_t;

void    conn_pool_init(void);
conn_t *conn_alloc(void);
void    conn_free(conn_t *c);
int     conn_index(const conn_t *c);
