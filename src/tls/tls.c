#define _GNU_SOURCE
#include <stdio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "tls.h"

static SSL_CTX *g_ctx;

int tls_init(const char *cert_file, const char *key_file)
{
    /* OpenSSL 1.1.0+ self-initialises; no explicit SSL_library_init needed. */
    g_ctx = SSL_CTX_new(TLS_server_method());
    if (!g_ctx) {
        ERR_print_errors_fp(stderr);
        return -1;
    }

    /*
     * Pin TLS 1.2 with an AES-GCM AEAD suite. This is deliberate: we run the
     * request path through io_uring recv on the raw fd, which only yields
     * plaintext if kTLS *RX* is active — and OpenSSL offloads kTLS RX for
     * TLS 1.2 only (TLS 1.3 gets kTLS TX but not RX, due to KeyUpdate
     * handling). Verified on Linux 6.x: 1.3 -> send=1 recv=0, 1.2 -> 1/1.
     * Pinning max to 1.2 keeps the whole data path in-kernel in both
     * directions. (set_cipher_list is the TLS 1.2 knob; set_ciphersuites is
     * TLS 1.3 only.)
     */
    SSL_CTX_set_min_proto_version(g_ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(g_ctx, TLS1_2_VERSION);
    if (SSL_CTX_set_cipher_list(g_ctx,
            "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256") != 1) {
        ERR_print_errors_fp(stderr);
        return -1;
    }

    /*
     * Ask OpenSSL to push the negotiated keys into the kernel TLS ULP after
     * the handshake. When this succeeds, OpenSSL itself calls
     * setsockopt(fd, SOL_TLS, TLS_TX/TLS_RX, &crypto_info) — so we never touch
     * raw key material. If the kernel or this OpenSSL build lacks kTLS, the
     * option is simply a no-op and tls_kactive() will report false.
     *
     * SSL_OP_NO_RENEGOTIATION: TLS 1.3 has no renegotiation, but be explicit;
     * a mid-stream rekey would invalidate our direct-to-fd splice writes.
     */
    SSL_CTX_set_options(g_ctx, SSL_OP_ENABLE_KTLS | SSL_OP_NO_RENEGOTIATION);

    if (SSL_CTX_use_certificate_chain_file(g_ctx, cert_file) != 1) {
        fprintf(stderr, "tls: failed to load cert %s\n", cert_file);
        ERR_print_errors_fp(stderr);
        return -1;
    }
    if (SSL_CTX_use_PrivateKey_file(g_ctx, key_file, SSL_FILETYPE_PEM) != 1) {
        fprintf(stderr, "tls: failed to load key %s\n", key_file);
        ERR_print_errors_fp(stderr);
        return -1;
    }
    if (SSL_CTX_check_private_key(g_ctx) != 1) {
        fprintf(stderr, "tls: cert/key mismatch\n");
        ERR_print_errors_fp(stderr);
        return -1;
    }
    return 0;
}

void *tls_new(int fd)
{
    SSL *ssl = SSL_new(g_ctx);
    if (!ssl)
        return NULL;
    /* SSL_set_fd uses BIO_NOCLOSE, so SSL_free will not close our fd —
     * conn_free owns the descriptor lifecycle. */
    if (SSL_set_fd(ssl, fd) != 1) {
        SSL_free(ssl);
        return NULL;
    }
    SSL_set_accept_state(ssl);
    return ssl;
}

int tls_do_handshake(void *vssl)
{
    SSL *ssl = (SSL *)vssl;
    int ret = SSL_accept(ssl);
    if (ret == 1)
        return TLS_HS_DONE;

    switch (SSL_get_error(ssl, ret)) {
    case SSL_ERROR_WANT_READ:  return TLS_HS_WANT_READ;
    case SSL_ERROR_WANT_WRITE: return TLS_HS_WANT_WRITE;
    default:
        return TLS_HS_ERROR;
    }
}

int tls_kactive(void *vssl)
{
    SSL *ssl = (SSL *)vssl;
    return BIO_get_ktls_send(SSL_get_wbio(ssl))
        && BIO_get_ktls_recv(SSL_get_rbio(ssl));
}

void tls_free(void *vssl)
{
    if (vssl)
        SSL_free((SSL *)vssl);
}

void tls_cleanup(void)
{
    if (g_ctx) {
        SSL_CTX_free(g_ctx);
        g_ctx = NULL;
    }
}
