#pragma once
#include <liburing.h>

int  server_setup_listen_socket(int port);
int  server_open_www_dir(const char *path);
void server_event_loop(struct io_uring *ring, int listen_fd, int www_dirfd);
#ifdef TLS_ENABLED
/* Register a second listening socket whose connections start with a TLS
 * handshake. Call before server_event_loop(). */
void server_set_tls_listener(int tls_listen_fd);
#endif
