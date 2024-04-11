#pragma once
#include <liburing.h>

int  server_setup_listen_socket(int port);
int  server_open_www_dir(const char *path);
void server_event_loop(struct io_uring *ring, int listen_fd, int www_dirfd);
