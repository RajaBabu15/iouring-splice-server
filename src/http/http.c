#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include "http.h"

int http_parse_request(const char *buf, int len, char *path_out, size_t path_max)
{
    if (len < 5 || memcmp(buf, "GET ", 4) != 0)
        return -1;
    const char *ps  = buf + 4;
    const char *end = NULL;
    for (int i = 0; i < len - 4; i++) {
        if (ps[i] == ' ' || ps[i] == '\r' || ps[i] == '\n') {
            end = ps + i;
            break;
        }
    }
    if (!end)
        return -1;
    size_t plen = (size_t)(end - ps);
    for (size_t i = 0; i < plen; i++) {    /* strip query string */
        if (ps[i] == '?') { plen = i; break; }
    }
    if (plen == 0 || plen >= path_max)
        return -1;
    memcpy(path_out, ps, plen);
    path_out[plen] = '\0';
    return 0;
}

const char *http_normalize_path(const char *url_path)
{
    if (!url_path || url_path[0] != '/')
        return NULL;
    const char *rel = url_path + 1;
    if (rel[0] == '\0')
        return "index.html";
    if (strstr(rel, "..") != NULL)      /* directory traversal guard */
        return NULL;
    return rel;
}

const char *http_mime_type(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot)                                                   return "application/octet-stream";
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) return "text/html";
    if (strcmp(dot, ".txt")  == 0)                              return "text/plain";
    if (strcmp(dot, ".css")  == 0)                              return "text/css";
    if (strcmp(dot, ".js")   == 0)                              return "application/javascript";
    if (strcmp(dot, ".json") == 0)                              return "application/json";
    if (strcmp(dot, ".png")  == 0)                              return "image/png";
    if (strcmp(dot, ".jpg")  == 0 || strcmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(dot, ".gif")  == 0)                              return "image/gif";
    return "application/octet-stream";
}

int http_build_200_header(char *buf, size_t bufsz,
                          long long content_length, const char *mime)
{
    return snprintf(buf, bufsz,
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: %lld\r\n"
        "Content-Type: %s\r\n"
        "Connection: close\r\n\r\n",
        content_length, mime);
}

int http_build_404_response(char *buf, size_t bufsz)
{
    static const char resp[] =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Length: 9\r\n"
        "Content-Type: text/plain\r\n"
        "Connection: close\r\n\r\n"
        "Not Found";
    size_t len = sizeof(resp) - 1;
    if (len >= bufsz)
        len = bufsz - 1;
    memcpy(buf, resp, len);
    return (int)len;
}
