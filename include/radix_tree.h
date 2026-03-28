#ifndef REDIS_CLONE_RADIX_TREE_H
#define REDIS_CLONE_RADIX_TREE_H

#include "common.h"

/* ============================================================
 *  Radix Tree (Compressed Trie / Patricia Trie)
 *
 *  Edges store substrings (compressed). Used for:
 *  - O(k) key lookup (k = key length, independent of N)
 *  - Prefix-based KEYS queries (KEYS user:*)
 *  - Sorted iteration over key space
 * ============================================================ */

#define RADIX_MAX_CHILDREN 256  /* one per byte value */

typedef struct radix_node {
    char                    *prefix;        /* edge label (substring) */
    char                    *value;         /* NULL if not a terminal node */
    bool                     has_value;     /* true if this node stores a key */
    char                     full_key[KV_MAX_KEY_LEN]; /* full key at this node */
    struct radix_node       *children[RADIX_MAX_CHILDREN];
    int                      child_count;
    atomic_int               ref_count;     /* for concurrent access */
} radix_node_t;

typedef struct {
    radix_node_t    *root;
    atomic_int       count;     /* number of stored keys */
    CRITICAL_SECTION lock;      /* write lock (readers don't lock) */
} radix_tree_t;

/* ---- API ---- */

/* Create a new radix tree. */
radix_tree_t *radix_tree_create(void);

/* Destroy radix tree. NOT thread-safe — call after shutdown. */
void radix_tree_destroy(radix_tree_t *rt);

/* Insert a key. Thread-safe (uses write lock). */
kv_status_t radix_tree_insert(radix_tree_t *rt, const char *key);

/* Delete a key. Thread-safe. */
kv_status_t radix_tree_delete(radix_tree_t *rt, const char *key);

/* Check if key exists. Thread-safe (lock-free read). */
bool radix_tree_contains(radix_tree_t *rt, const char *key);

/* Find all keys matching a prefix (e.g. "user:*").
 * Returns count of matching keys. Caller provides keys buffer. */
int radix_tree_prefix_search(radix_tree_t *rt, const char *prefix,
                              char keys[][KV_MAX_KEY_LEN], int max_keys);

/* Get total key count. */
int radix_tree_count(radix_tree_t *rt);

#endif /* REDIS_CLONE_RADIX_TREE_H */
