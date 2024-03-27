#pragma once
#include <stddef.h>

int         http_parse_request(const char *buf, int len, char *path_out, size_t path_max);
const char *http_normalize_path(const char *url_path);
