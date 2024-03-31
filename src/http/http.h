#pragma once
#include <stddef.h>

int         http_parse_request(const char *buf, int len, char *path_out, size_t path_max);
const char *http_normalize_path(const char *url_path);
const char *http_mime_type(const char *path);
int         http_build_200_header(char *buf, size_t bufsz,
                                  long long content_length, const char *mime);
int         http_build_404_response(char *buf, size_t bufsz);
