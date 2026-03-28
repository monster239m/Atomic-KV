/*
 * main.c — Redis Clone Entry Point
 *
 * Initializes all subsystems and starts the server.
 * Ctrl+C triggers graceful shutdown.
 */

#include "server.h"

static kv_server_t *g_server = NULL;

/* Ctrl+C handler */
static BOOL WINAPI ctrl_handler(DWORD dwType) {
    if (dwType == CTRL_C_EVENT || dwType == CTRL_CLOSE_EVENT) {
        if (g_server) {
            server_stop(g_server);
        }
        return TRUE;
    }
    return FALSE;
}

int main(int argc, char *argv[]) {
    int port = KV_DEFAULT_PORT;

    /* Parse optional port argument */
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Usage: %s [port]\n", argv[0]);
            return 1;
        }
    }

    printf("[INIT] Seeding random number generator...\n");
    srand((unsigned int)time(NULL));

    /* Create hash map */
    printf("[INIT] Creating hash map (%d buckets)...\n", KV_HASHMAP_BUCKETS);
    hashmap_t *hm = hashmap_create();
    if (!hm) {
        fprintf(stderr, "[FATAL] Failed to create hash map\n");
        return 1;
    }

    /* Create radix tree */
    printf("[INIT] Creating radix tree...\n");
    radix_tree_t *rt = radix_tree_create();
    if (!rt) {
        fprintf(stderr, "[FATAL] Failed to create radix tree\n");
        hashmap_destroy(hm);
        return 1;
    }

    /* Create server */
    printf("[INIT] Creating server on port %d...\n", port);
    kv_server_t *srv = server_create(port, hm, rt);
    if (!srv) {
        fprintf(stderr, "[FATAL] Failed to create server\n");
        radix_tree_destroy(rt);
        hashmap_destroy(hm);
        return 1;
    }

    g_server = srv;
    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    /* Start server */
    kv_status_t status = server_start(srv);
    if (status != KV_OK) {
        fprintf(stderr, "[FATAL] Failed to start server: %s\n",
                kv_status_str(status));
        server_destroy(srv);
        radix_tree_destroy(rt);
        hashmap_destroy(hm);
        return 1;
    }

    /* Block main thread — wait for shutdown signal */
    while (atomic_load(&srv->running)) {
        Sleep(500);
    }

    /* Cleanup */
    printf("[CLEANUP] Destroying server...\n");
    server_destroy(srv);
    printf("[CLEANUP] Destroying radix tree...\n");
    radix_tree_destroy(rt);
    printf("[CLEANUP] Destroying hash map...\n");
    hashmap_destroy(hm);
    printf("[CLEANUP] Done. Goodbye!\n");

    return 0;
}
