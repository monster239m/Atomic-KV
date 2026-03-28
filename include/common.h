#ifndef REDIS_CLONE_COMMON_H
#define REDIS_CLONE_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <time.h>
#include <windows.h>

/* ============================================================
 *  Configuration
 * ============================================================ */
#define KV_MAX_KEY_LEN      256
#define KV_MAX_VALUE_LEN    4096
#define KV_DEFAULT_PORT     6380
#define KV_WORKER_THREADS   4
#define KV_HASHMAP_BUCKETS  65536    /* must be power of 2 */
#define KV_RING_BUFFER_SIZE 16384    /* must be power of 2 */
#define KV_MAX_CLIENTS      1024

/* Skip list tuning */
#define SKIPLIST_MAX_LEVEL  32
#define SKIPLIST_P          0.25     /* probability of level promotion */

/* Cache line size (x86) */
#define CACHE_LINE_SIZE     64

/* ============================================================
 *  Alignment Macros
 * ============================================================ */
#define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE_SIZE)))

/* ============================================================
 *  Status Codes
 * ============================================================ */
typedef enum {
    KV_OK = 0,
    KV_ERR_NOT_FOUND,
    KV_ERR_FULL,
    KV_ERR_EMPTY,
    KV_ERR_EXISTS,
    KV_ERR_MEMORY,
    KV_ERR_INVALID,
    KV_ERR_NETWORK,
    KV_ERR_SHUTDOWN
} kv_status_t;

static inline const char *kv_status_str(kv_status_t s) {
    switch (s) {
        case KV_OK:             return "OK";
        case KV_ERR_NOT_FOUND:  return "NOT_FOUND";
        case KV_ERR_FULL:       return "FULL";
        case KV_ERR_EMPTY:      return "EMPTY";
        case KV_ERR_EXISTS:     return "EXISTS";
        case KV_ERR_MEMORY:     return "OUT_OF_MEMORY";
        case KV_ERR_INVALID:    return "INVALID";
        case KV_ERR_NETWORK:    return "NETWORK_ERROR";
        case KV_ERR_SHUTDOWN:   return "SHUTDOWN";
        default:                return "UNKNOWN";
    }
}

/* ============================================================
 *  Command Types
 * ============================================================ */
typedef enum {
    CMD_SET,
    CMD_GET,
    CMD_DEL,
    CMD_KEYS,
    CMD_RANGE,
    CMD_PING,
    CMD_INFO,
    CMD_QUIT,
    CMD_BENCH,
    CMD_UNKNOWN
} cmd_type_t;

/* ============================================================
 *  Command Structure
 * ============================================================ */
typedef struct {
    cmd_type_t  type;
    char        key[KV_MAX_KEY_LEN];
    char        value[KV_MAX_VALUE_LEN];
    char        end_key[KV_MAX_KEY_LEN];   /* for RANGE queries */
} kv_command_t;

/* ============================================================
 *  FNV-1a Hash Function (fast, low collision for strings)
 * ============================================================ */
static inline uint32_t fnv1a_hash(const char *key) {
    uint32_t hash = 2166136261u; /* FNV offset basis */
    while (*key) {
        hash ^= (uint8_t)*key++;
        hash *= 16777619u;       /* FNV prime */
    }
    return hash;
}

/* ============================================================
 *  Utility
 * ============================================================ */
static inline int kv_random_level(void) {
    int level = 1;
    while (level < SKIPLIST_MAX_LEVEL && ((double)rand() / RAND_MAX) < SKIPLIST_P)
        level++;
    return level;
}

/* Thread-safe timestamp in milliseconds */
static inline uint64_t kv_timestamp_ms(void) {
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (uint64_t)(counter.QuadPart * 1000 / freq.QuadPart);
}

#endif /* REDIS_CLONE_COMMON_H */
