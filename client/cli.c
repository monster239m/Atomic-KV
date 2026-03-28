/*
 * cli.c — Interactive CLI Client for the Redis Clone
 *
 * Connects to the server and provides a REPL interface.
 * Supports built-in BENCH command for stress testing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

/* String comparison — MinGW compatibility */
extern int _stricmp(const char *, const char *);
extern int _strnicmp(const char *, const char *, size_t);

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 6380
#define BUF_SIZE     65536

static SOCKET connect_to_server(const char *host, int port) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return INVALID_SOCKET;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "socket() failed\n");
        return INVALID_SOCKET;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = inet_addr(host);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "connect() failed: %d\n", WSAGetLastError());
        closesocket(sock);
        return INVALID_SOCKET;
    }

    return sock;
}

/* ============================================================
 *  Built-in BENCH command: multi-threaded stress test
 * ============================================================ */
typedef struct {
    const char *host;
    int         port;
    int         num_ops;
    int         thread_id;
    LARGE_INTEGER freq;
    double      elapsed_ms;
    int         success_count;
} bench_ctx_t;

static DWORD WINAPI bench_thread(LPVOID arg) {
    bench_ctx_t *ctx = (bench_ctx_t *)arg;
    SOCKET sock = connect_to_server(ctx->host, ctx->port);
    if (sock == INVALID_SOCKET) {
        ctx->success_count = 0;
        return 1;
    }

    char send_buf[512];
    char recv_buf[BUF_SIZE];
    int success = 0;

    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);

    for (int i = 0; i < ctx->num_ops; i++) {
        int op = i % 3; /* cycle: SET, GET, DEL */
        int key_id = ctx->thread_id * ctx->num_ops + i;

        if (op == 0) {
            snprintf(send_buf, sizeof(send_buf),
                     "SET bench:%d:key%d value%d\n", ctx->thread_id, key_id, i);
        } else if (op == 1) {
            snprintf(send_buf, sizeof(send_buf),
                     "GET bench:%d:key%d\n", ctx->thread_id, key_id - 1);
        } else {
            snprintf(send_buf, sizeof(send_buf),
                     "DEL bench:%d:key%d\n", ctx->thread_id, key_id - 2);
        }

        if (send(sock, send_buf, (int)strlen(send_buf), 0) > 0) {
            int bytes = recv(sock, recv_buf, BUF_SIZE - 1, 0);
            if (bytes > 0) success++;
        }
    }

    QueryPerformanceCounter(&end);
    ctx->elapsed_ms = (double)(end.QuadPart - start.QuadPart) * 1000.0 / ctx->freq.QuadPart;
    ctx->success_count = success;

    closesocket(sock);
    WSACleanup();
    return 0;
}

static void run_benchmark(const char *host, int port, int num_ops, int num_threads) {
    printf("\n=== BENCHMARK ===\n");
    printf("Operations per thread: %d\n", num_ops);
    printf("Threads: %d\n", num_threads);
    printf("Total operations: %d\n", num_ops * num_threads);
    printf("Running...\n\n");

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    bench_ctx_t *contexts = (bench_ctx_t *)calloc(num_threads, sizeof(bench_ctx_t));
    HANDLE *threads = (HANDLE *)calloc(num_threads, sizeof(HANDLE));

    LARGE_INTEGER total_start, total_end;
    QueryPerformanceCounter(&total_start);

    for (int i = 0; i < num_threads; i++) {
        contexts[i].host = host;
        contexts[i].port = port;
        contexts[i].num_ops = num_ops;
        contexts[i].thread_id = i;
        contexts[i].freq = freq;
        threads[i] = CreateThread(NULL, 0, bench_thread, &contexts[i], 0, NULL);
    }

    WaitForMultipleObjects(num_threads, threads, TRUE, INFINITE);
    QueryPerformanceCounter(&total_end);

    double total_ms = (double)(total_end.QuadPart - total_start.QuadPart) * 1000.0 / freq.QuadPart;
    int total_success = 0;

    printf("Thread Results:\n");
    for (int i = 0; i < num_threads; i++) {
        printf("  Thread %d: %d/%d ops in %.1f ms (%.0f ops/sec)\n",
               i, contexts[i].success_count, num_ops,
               contexts[i].elapsed_ms,
               contexts[i].success_count / (contexts[i].elapsed_ms / 1000.0));
        total_success += contexts[i].success_count;
        CloseHandle(threads[i]);
    }

    int total_ops = num_ops * num_threads;
    printf("\n--- Summary ---\n");
    printf("Total: %d/%d ops in %.1f ms\n", total_success, total_ops, total_ms);
    printf("Throughput: %.0f ops/sec\n", total_success / (total_ms / 1000.0));
    printf("Avg latency: %.3f ms/op\n", total_ms / total_success);
    printf("===============\n\n");

    free(contexts);
    free(threads);
}

/* ============================================================
 *  Main REPL
 * ============================================================ */
int main(int argc, char *argv[]) {
    const char *host = DEFAULT_HOST;
    int port = DEFAULT_PORT;

    if (argc > 1) host = argv[1];
    if (argc > 2) port = atoi(argv[2]);

    printf("Redis Clone CLI - Connecting to %s:%d...\n", host, port);

    SOCKET sock = connect_to_server(host, port);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Failed to connect to %s:%d\n", host, port);
        return 1;
    }

    printf("Connected! Type 'help' for commands, 'quit' to exit.\n\n");

    char input[4096];
    char recv_buf[BUF_SIZE];

    for (;;) {
        printf("redis-clone> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) break;

        /* Trim newline */
        int len = (int)strlen(input);
        while (len > 0 && (input[len - 1] == '\n' || input[len - 1] == '\r'))
            input[--len] = '\0';

        if (len == 0) continue;

        /* Built-in help */
        if (_stricmp(input, "help") == 0) {
            printf("Commands:\n");
            printf("  SET key value    - Store a key-value pair\n");
            printf("  GET key          - Retrieve a value by key\n");
            printf("  DEL key          - Delete a key\n");
            printf("  KEYS prefix*     - List keys matching prefix\n");
            printf("  RANGE start end  - Range query on keys\n");
            printf("  PING             - Ping the server\n");
            printf("  INFO             - Server information\n");
            printf("  BENCH n threads  - Benchmark (n ops per thread)\n");
            printf("  QUIT             - Disconnect\n\n");
            continue;
        }

        /* Built-in BENCH command (handled client-side) */
        if (_strnicmp(input, "bench", 5) == 0) {
            int ops = 10000, threads = 4;
            sscanf(input + 5, "%d %d", &ops, &threads);
            if (ops < 1) ops = 1;
            if (threads < 1) threads = 1;
            if (threads > 64) threads = 64;
            run_benchmark(host, port, ops, threads);
            continue;
        }

        /* Send command to server */
        strcat(input, "\n");
        if (send(sock, input, (int)strlen(input), 0) <= 0) {
            fprintf(stderr, "Connection lost\n");
            break;
        }

        /* Check for quit */
        if (_strnicmp(input, "quit", 4) == 0 || _strnicmp(input, "exit", 4) == 0) {
            int bytes = recv(sock, recv_buf, BUF_SIZE - 1, 0);
            if (bytes > 0) {
                recv_buf[bytes] = '\0';
                printf("%s", recv_buf);
            }
            break;
        }

        /* Receive response */
        int bytes = recv(sock, recv_buf, BUF_SIZE - 1, 0);
        if (bytes <= 0) {
            fprintf(stderr, "Connection lost\n");
            break;
        }
        recv_buf[bytes] = '\0';

        /* Pretty-print response */
        if (recv_buf[0] == '+') {
            printf("%s", recv_buf + 1);
        } else if (recv_buf[0] == '$') {
            if (strcmp(recv_buf, "$-1\r\n") == 0) {
                printf("(nil)\n");
            } else {
                printf("%s", recv_buf + 1);
            }
        } else if (recv_buf[0] == ':') {
            printf("(integer) %s", recv_buf + 1);
        } else if (recv_buf[0] == '*') {
            /* Array response */
            char *line = recv_buf;
            char *next;
            int first = 1;
            while ((next = strstr(line, "\r\n")) != NULL) {
                *next = '\0';
                if (first) {
                    int count = atoi(line + 1);
                    if (count == 0) printf("(empty list)\n");
                    first = 0;
                } else if (line[0] == '$') {
                    printf("  %s\n", line + 1);
                }
                line = next + 2;
            }
        } else if (recv_buf[0] == '-') {
            printf("ERROR: %s", recv_buf + 1);
        } else {
            printf("%s", recv_buf);
        }
    }

    closesocket(sock);
    WSACleanup();
    printf("Disconnected.\n");
    return 0;
}
