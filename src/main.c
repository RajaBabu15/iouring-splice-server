#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_PORT 8080
#define WWW_DIR      "./www"

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);

    int port = DEFAULT_PORT;
    if (argc > 1) port = atoi(argv[1]);

    printf("starting io_uring splice server on :%d  www=%s\n", port, WWW_DIR);
    fflush(stdout);

    /* TODO: io_uring init, listen socket, accept/recv/splice event loop */
    return 0;
}
