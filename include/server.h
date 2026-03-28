#ifndef REDIS_CLONE_SERVER_H
#define REDIS_CLONE_SERVER_H

#include "common.h"
#include "hashmap.h"
#include "radix_tree.h"
#include "ring_buffer.h"

/* ============================================================
 *  TCP Server (Multi-threaded, Winsock2)
 *
 *  - Accepts connections on configurable port
 *  - Each client gets its own handler thread
 *  - Commands are parsed and executed directly in handler thread
 *    (for simplicity and lower latency vs ring buffer queueing)
 * ============================================================ */

typedef struct {
    SOCKET          listen_sock;
    hashmap_t      *hashmap;
    radix_tree_t   *radix_tree;
    int             port;
    atomic_int      running;         /* 1 = running, 0 = shutdown */
    atomic_int      client_count;
    HANDLE          accept_thread;
} kv_server_t;

/* ---- API ---- */

/* Create and initialize server (does not start listening yet). */
kv_server_t *server_create(int port, hashmap_t *hm, radix_tree_t *rt);

/* Start server: bind, listen, accept loop in background thread.
 * Returns KV_OK on success. */
kv_status_t server_start(kv_server_t *srv);

/* Stop server gracefully. */
void server_stop(kv_server_t *srv);

/* Destroy server and free resources. */
void server_destroy(kv_server_t *srv);

#endif /* REDIS_CLONE_SERVER_H */
