#pragma once

/*
 * Thin wrapper around OpenSSL for the TLS handshake + kernel-TLS (kTLS)
 * handover. Deliberately OpenSSL-header-free so the rest of the codebase
 * (conn/server/main) can call it without pulling in OpenSSL headers — the SSL
 * object is passed around as an opaque void*. Only tls.c links OpenSSL, and
 * the whole module is compiled in only under -DTLS_ENABLED.
 */

/* Result of one non-blocking SSL_accept step. */
enum tls_hs {
    TLS_HS_DONE       =  0,   /* handshake complete */
    TLS_HS_WANT_READ  =  1,   /* poll POLLIN  then call tls_do_handshake again */
    TLS_HS_WANT_WRITE =  2,   /* poll POLLOUT then call tls_do_handshake again */
    TLS_HS_ERROR      = -1,   /* fatal — drop the connection */
};

/*
 * Process-wide init: build the SSL_CTX, load cert/key, pin TLS 1.3 +
 * AES-128-GCM (the cipher with the broadest kTLS RX support), and request
 * kTLS via SSL_OP_ENABLE_KTLS. Returns 0 on success, -1 on failure.
 */
int  tls_init(const char *cert_file, const char *key_file);

/* Create a server-side SSL bound to fd. Returns opaque SSL* or NULL. */
void *tls_new(int fd);

/* Drive one step of the handshake. See enum tls_hs. */
int  tls_do_handshake(void *ssl);

/*
 * After TLS_HS_DONE, report whether kTLS is active in *both* directions.
 * We require TX (zero-copy encrypted send via splice) and RX (so io_uring
 * recv on the raw fd yields plaintext). Returns 1 if both active, else 0.
 */
int  tls_kactive(void *ssl);

/* Free a per-connection SSL (does not close the fd — BIO is NOCLOSE). */
void tls_free(void *ssl);

/* Free the process-wide SSL_CTX. */
void tls_cleanup(void);
