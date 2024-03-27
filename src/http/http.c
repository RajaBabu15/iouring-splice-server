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
    return rel;
}
