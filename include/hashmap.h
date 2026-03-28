#ifndef REDIS_CLONE_HASHMAP_H
#define REDIS_CLONE_HASHMAP_H

#include "common.h"
#include "skiplist.h"

/* ============================================================
 *  Concurrent Hash Map
 *
 *  Fixed-size bucket array. Each bucket holds one skip list.
 *  Buckets are independent → naturally concurrent across buckets.
 *  Uses FNV-1a hash to route keys to buckets.
 * ============================================================ */

typedef struct {
    skiplist_t     *buckets[KV_HASHMAP_BUCKETS];
    atomic_long     total_ops;       /* stats: total operations */
    atomic_long     total_keys;      /* stats: approximate key count */
} hashmap_t;

/* ---- API ---- */

/* Create a new hash map. Returns NULL on failure. */
hashmap_t *hashmap_create(void);

/* Destroy hash map and all buckets. NOT thread-safe — call after shutdown. */
void hashmap_destroy(hashmap_t *hm);

/* SET key=value. Thread-safe. */
kv_status_t hashmap_set(hashmap_t *hm, const char *key, const char *value);

/* GET key → out_value. Thread-safe. */
kv_status_t hashmap_get(hashmap_t *hm, const char *key, char *out_value);

/* DEL key. Thread-safe. */
kv_status_t hashmap_del(hashmap_t *hm, const char *key);

/* Approximate total key count. */
long hashmap_key_count(hashmap_t *hm);

/* Total operations performed. */
long hashmap_op_count(hashmap_t *hm);

#endif /* REDIS_CLONE_HASHMAP_H */
