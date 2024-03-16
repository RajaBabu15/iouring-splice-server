#define _GNU_SOURCE
#include <string.h>
#include <unistd.h>
#include "conn.h"

static conn_t conn_pool[MAX_CONNECTIONS];
static int    free_list_head = -1;

void conn_pool_init(void)
{
    for (int i = 0; i < MAX_CONNECTIONS - 1; i++)
        conn_pool[i].free_next = i + 1;
    conn_pool[MAX_CONNECTIONS - 1].free_next = -1;
    free_list_head = 0;
}

conn_t *conn_alloc(void)
{
    if (free_list_head < 0)
        return NULL;
    int idx = free_list_head;
    free_list_head = conn_pool[idx].free_next;
    conn_t *c = &conn_pool[idx];
    memset(c, 0, sizeof(*c));
    c->in_use    = true;
    c->sock_fd   = -1;
    c->file_fd   = -1;
    c->pipe_rd   = -1;
    c->pipe_wr   = -1;
    c->free_next = -1;
    return c;
}

void conn_free(conn_t *c)
{
    if (!c->in_use)
        return;
    if (c->sock_fd >= 0) { close(c->sock_fd); c->sock_fd = -1; }
    if (c->file_fd >= 0) { close(c->file_fd); c->file_fd = -1; }
    if (c->pipe_rd >= 0) { close(c->pipe_rd); c->pipe_rd = -1; }
    if (c->pipe_wr >= 0) { close(c->pipe_wr); c->pipe_wr = -1; }
    c->in_use = false;
    int idx = (int)(c - conn_pool);
    c->free_next   = free_list_head;
    free_list_head = idx;
}

int conn_index(const conn_t *c)
{
    return (int)(c - conn_pool);
}
