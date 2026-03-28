#ifndef REDIS_CLONE_SKIPLIST_H
#define REDIS_CLONE_SKIPLIST_H

#include "common.h"

/* ============================================================
 *  Skip List Node (lock-free)
 *
 *  Each node has an array of atomic next-pointers (one per level).
 *  A node is "logically deleted" when its `marked` flag is set.
 *  Physical removal happens during traversal (lazy cleanup).
 * ============================================================ */
typedef struct skiplist_node {
    char            key[KV_MAX_KEY_LEN];
    char            value[KV_MAX_VALUE_LEN];
    int             level;
    atomic_int      marked;         /* 1 = logically deleted */
    atomic_int      ref_count;      /* for safe memory reclamation */
    /* next[i] points to the next node at level i.
     * We store as atomic uintptr_t for CAS operations. */
    atomic_uintptr_t next[SKIPLIST_MAX_LEVEL];
} skiplist_node_t;

/* ============================================================
 *  Skip List (lock-free concurrent sorted structure)
 * ============================================================ */
typedef struct {
    skiplist_node_t *header;        /* sentinel head node */
    atomic_int       count;         /* number of elements */
    atomic_int       max_level;     /* current max level in use */
} skiplist_t;

/* ---- API ---- */

/* Create a new skip list. Returns NULL on failure. */
skiplist_t *skiplist_create(void);

/* Destroy skip list and free all nodes. NOT thread-safe — call after all threads stop. */
void skiplist_destroy(skiplist_t *sl);

/* Insert or update key-value pair. Thread-safe (lock-free). */
kv_status_t skiplist_insert(skiplist_t *sl, const char *key, const char *value);

/* Search for a key. Copies value into `out_value` (must be >= KV_MAX_VALUE_LEN).
 * Returns KV_OK if found, KV_ERR_NOT_FOUND otherwise. Thread-safe. */
kv_status_t skiplist_search(skiplist_t *sl, const char *key, char *out_value);

/* Delete a key. Thread-safe (lock-free logical deletion). */
kv_status_t skiplist_delete(skiplist_t *sl, const char *key);

/* Get current element count. Thread-safe. */
int skiplist_count(skiplist_t *sl);

/* Collect all keys into `keys` array (up to max_keys). Returns count found.
 * Caller must allocate keys as char[max_keys][KV_MAX_KEY_LEN]. */
int skiplist_collect_keys(skiplist_t *sl, char keys[][KV_MAX_KEY_LEN], int max_keys);

/* Collect keys in range [start_key, end_key]. Returns count found. */
int skiplist_range(skiplist_t *sl, const char *start_key, const char *end_key,
                   char keys[][KV_MAX_KEY_LEN], int max_keys);

#endif /* REDIS_CLONE_SKIPLIST_H */
