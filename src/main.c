#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <liburing.h>
#include "conn/conn.h"
#include "ring/ring.h"
#include "server/server.h"

#define DEFAULT_PORT 8080
#define WWW_DIR      "./www"

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);

    int port = DEFAULT_PORT;
    if (argc > 1) port = atoi(argv[1]);

    struct io_uring ring;

    int listen_fd = server_setup_listen_socket(port);
    ring_init(&ring);
    int www_dirfd = server_open_www_dir(WWW_DIR);
    conn_pool_init();

    printf("listening on :%d  www=%s\n", port, WWW_DIR);
    fflush(stdout);

    server_event_loop(&ring, listen_fd, www_dirfd);
    return 0;
}
