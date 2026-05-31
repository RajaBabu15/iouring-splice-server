#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <liburing.h>
#include "conn/conn.h"
#include "ring/ring.h"
#include "server/server.h"
#ifdef TLS_ENABLED
#include "tls/tls.h"
#endif

#define DEFAULT_PORT     8080
#define DEFAULT_TLS_PORT 8443
#define WWW_DIR          "./www"

static void usage(const char *argv0)
{
    fprintf(stderr,
        "usage: %s [http_port]"
#ifdef TLS_ENABLED
        " [--tls-cert FILE --tls-key FILE] [--tls-port PORT]"
#endif
        "\n", argv0);
}

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);

    int port = DEFAULT_PORT;
#ifdef TLS_ENABLED
    int         tls_port = DEFAULT_TLS_PORT;
    const char *tls_cert = NULL;
    const char *tls_key  = NULL;
#endif

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            port = atoi(argv[i]);
            continue;
        }
#ifdef TLS_ENABLED
        if (strcmp(argv[i], "--tls-cert") == 0 && i + 1 < argc) {
            tls_cert = argv[++i];
        } else if (strcmp(argv[i], "--tls-key") == 0 && i + 1 < argc) {
            tls_key = argv[++i];
        } else if (strcmp(argv[i], "--tls-port") == 0 && i + 1 < argc) {
            tls_port = atoi(argv[++i]);
        } else {
            usage(argv[0]);
            return 1;
        }
#else
        if (strncmp(argv[i], "--tls", 5) == 0) {
            fprintf(stderr, "this binary was built without TLS; "
                            "rebuild with 'make build-tls'\n");
            return 1;
        }
        usage(argv[0]);
        return 1;
#endif
    }

#ifdef TLS_ENABLED
    if ((tls_cert != NULL) != (tls_key != NULL)) {
        fprintf(stderr, "--tls-cert and --tls-key must be given together\n");
        return 1;
    }
#endif

    struct io_uring ring;

    int listen_fd = server_setup_listen_socket(port);
    ring_init(&ring);
    int www_dirfd = server_open_www_dir(WWW_DIR);
    conn_pool_init();

    printf("listening on :%d  www=%s\n", port, WWW_DIR);

#ifdef TLS_ENABLED
    if (tls_cert) {
        if (tls_init(tls_cert, tls_key) < 0) {
            fprintf(stderr, "tls_init failed\n");
            return 1;
        }
        int tls_fd = server_setup_listen_socket(tls_port);
        server_set_tls_listener(tls_fd);
        printf("TLS listening on :%d  (cert=%s)\n", tls_port, tls_cert);
    }
#endif
    fflush(stdout);

    server_event_loop(&ring, listen_fd, www_dirfd);

#ifdef TLS_ENABLED
    tls_cleanup();
#endif
    return 0;
}
