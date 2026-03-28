/*
 * server.c — Multi-Threaded TCP Server (Winsock2)
 *
 * Accepts client connections. Each client gets a handler thread.
 * Commands are parsed and executed directly in the handler thread
 * for minimum latency.
 */

#include "server.h"
#include "command.h"


#define RECV_BUF_SIZE 8192
#define SEND_BUF_SIZE (KV_MAX_VALUE_LEN * 4 + 4096)

/* ============================================================
 *  Client handler context
 * ============================================================ */
typedef struct {
    SOCKET          client_sock;
    kv_server_t    *server;
} client_ctx_t;

/* ============================================================
 *  Client handler thread
 * ============================================================ */
static DWORD WINAPI client_handler(LPVOID arg) {
    client_ctx_t *ctx = (client_ctx_t *)arg;
    SOCKET sock = ctx->client_sock;
    kv_server_t *srv = ctx->server;
    free(ctx);

    char recv_buf[RECV_BUF_SIZE];
    char send_buf[SEND_BUF_SIZE];

    atomic_fetch_add(&srv->client_count, 1);

    while (atomic_load(&srv->running)) {
        int bytes = recv(sock, recv_buf, RECV_BUF_SIZE - 1, 0);
        if (bytes <= 0) break; /* disconnect or error */

        recv_buf[bytes] = '\0';

        /* Process each line (supports pipelined commands) */
        char *line = strtok(recv_buf, "\n");
        while (line) {
            kv_command_t cmd;
            kv_status_t parse_status = command_parse(line, &cmd);

            if (parse_status == KV_OK) {
                if (cmd.type == CMD_QUIT) {
                    send(sock, "+BYE\r\n", 6, 0);
                    goto disconnect;
                }
                int resp_len = command_execute(&cmd, srv->hashmap,
                                                srv->radix_tree,
                                                send_buf, SEND_BUF_SIZE);
                if (resp_len > 0) {
                    send(sock, send_buf, resp_len, 0);
                }
            } else {
                int resp_len = snprintf(send_buf, SEND_BUF_SIZE,
                                        "-ERR invalid command\r\n");
                send(sock, send_buf, resp_len, 0);
            }

            line = strtok(NULL, "\n");
        }
    }

disconnect:
    closesocket(sock);
    atomic_fetch_sub(&srv->client_count, 1);
    return 0;
}

/* ============================================================
 *  Accept loop thread
 * ============================================================ */
static DWORD WINAPI accept_loop(LPVOID arg) {
    kv_server_t *srv = (kv_server_t *)arg;

    while (atomic_load(&srv->running)) {
        struct sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);

        SOCKET client_sock = accept(srv->listen_sock,
                                     (struct sockaddr *)&client_addr,
                                     &addr_len);
        if (client_sock == INVALID_SOCKET) {
            if (!atomic_load(&srv->running)) break;
            continue;
        }

        /* Spawn handler thread for this client */
        client_ctx_t *ctx = (client_ctx_t *)malloc(sizeof(client_ctx_t));
        if (!ctx) {
            closesocket(client_sock);
            continue;
        }
        ctx->client_sock = client_sock;
        ctx->server = srv;

        HANDLE thread = CreateThread(NULL, 0, client_handler, ctx, 0, NULL);
        if (thread) {
            CloseHandle(thread); /* detach — handler is self-managing */
        } else {
            closesocket(client_sock);
            free(ctx);
        }
    }

    return 0;
}

/* ============================================================
 *  Server Create / Start / Stop / Destroy
 * ============================================================ */
kv_server_t *server_create(int port, hashmap_t *hm, radix_tree_t *rt) {
    kv_server_t *srv = (kv_server_t *)calloc(1, sizeof(kv_server_t));
    if (!srv) return NULL;

    srv->port = port > 0 ? port : KV_DEFAULT_PORT;
    srv->hashmap = hm;
    srv->radix_tree = rt;
    srv->listen_sock = INVALID_SOCKET;
    atomic_store(&srv->running, 0);
    atomic_store(&srv->client_count, 0);
    return srv;
}

kv_status_t server_start(kv_server_t *srv) {
    if (!srv) return KV_ERR_INVALID;

    /* Initialize Winsock */
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "[ERROR] WSAStartup failed: %d\n", WSAGetLastError());
        return KV_ERR_NETWORK;
    }

    /* Create socket */
    srv->listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv->listen_sock == INVALID_SOCKET) {
        fprintf(stderr, "[ERROR] socket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return KV_ERR_NETWORK;
    }

    /* Allow reuse */
    int opt = 1;
    setsockopt(srv->listen_sock, SOL_SOCKET, SO_REUSEADDR,
               (char *)&opt, sizeof(opt));

    /* Bind */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((unsigned short)srv->port);

    if (bind(srv->listen_sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "[ERROR] bind() failed on port %d: %d\n",
                srv->port, WSAGetLastError());
        closesocket(srv->listen_sock);
        WSACleanup();
        return KV_ERR_NETWORK;
    }

    /* Listen */
    if (listen(srv->listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        fprintf(stderr, "[ERROR] listen() failed: %d\n", WSAGetLastError());
        closesocket(srv->listen_sock);
        WSACleanup();
        return KV_ERR_NETWORK;
    }

    atomic_store(&srv->running, 1);

    /* Start accept loop in background thread */
    srv->accept_thread = CreateThread(NULL, 0, accept_loop, srv, 0, NULL);
    if (!srv->accept_thread) {
        fprintf(stderr, "[ERROR] Failed to create accept thread\n");
        closesocket(srv->listen_sock);
        WSACleanup();
        return KV_ERR_NETWORK;
    }

    printf("==================================================\n");
    printf("  Redis Clone Server v1.0\n");
    printf("  Listening on port %d\n", srv->port);
    printf("  Buckets: %d | Workers: per-client threads\n", KV_HASHMAP_BUCKETS);
    printf("  Press Ctrl+C to shutdown\n");
    printf("==================================================\n\n");
    fflush(stdout);

    return KV_OK;
}

void server_stop(kv_server_t *srv) {
    if (!srv) return;

    printf("\n[INFO] Shutting down server...\n");
    fflush(stdout);

    atomic_store(&srv->running, 0);

    /* Close listening socket to unblock accept() */
    if (srv->listen_sock != INVALID_SOCKET) {
        closesocket(srv->listen_sock);
        srv->listen_sock = INVALID_SOCKET;
    }

    /* Wait for accept thread */
    if (srv->accept_thread) {
        WaitForSingleObject(srv->accept_thread, 3000);
        CloseHandle(srv->accept_thread);
        srv->accept_thread = NULL;
    }

    /* Wait for remaining clients (give them 2 seconds) */
    int wait = 0;
    while (atomic_load(&srv->client_count) > 0 && wait < 20) {
        Sleep(100);
        wait++;
    }

    printf("[INFO] Server stopped. Total ops: %ld, Keys: %ld\n",
           hashmap_op_count(srv->hashmap),
           hashmap_key_count(srv->hashmap));
    fflush(stdout);
}

void server_destroy(kv_server_t *srv) {
    if (!srv) return;
    WSACleanup();
    free(srv);
}
