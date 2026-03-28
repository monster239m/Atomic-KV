/*
 * hashmap.c — Concurrent Hash Map
 *
 * Routes keys to buckets via FNV-1a hash.
 * Each bucket is an independent skip list → naturally lock-free across buckets.
 */

#include "hashmap.h"

hashmap_t *hashmap_create(void) {
    hashmap_t *hm = (hashmap_t *)calloc(1, sizeof(hashmap_t));
    if (!hm) return NULL;

    for (int i = 0; i < KV_HASHMAP_BUCKETS; i++) {
        hm->buckets[i] = skiplist_create();
        if (!hm->buckets[i]) {
            /* Cleanup on failure */
            for (int j = 0; j < i; j++) {
                skiplist_destroy(hm->buckets[j]);
            }
            free(hm);
            return NULL;
        }
    }
    atomic_store(&hm->total_ops, 0);
    atomic_store(&hm->total_keys, 0);
    return hm;
}

void hashmap_destroy(hashmap_t *hm) {
    if (!hm) return;
    for (int i = 0; i < KV_HASHMAP_BUCKETS; i++) {
        if (hm->buckets[i]) {
            skiplist_destroy(hm->buckets[i]);
        }
    }
    free(hm);
}

static inline int bucket_index(const char *key) {
    return fnv1a_hash(key) & (KV_HASHMAP_BUCKETS - 1);
}

kv_status_t hashmap_set(hashmap_t *hm, const char *key, const char *value) {
    if (!hm || !key || !value) return KV_ERR_INVALID;

    int idx = bucket_index(key);
    skiplist_t *sl = hm->buckets[idx];

    /* Check if key exists first to track count accurately */
    char tmp[KV_MAX_VALUE_LEN];
    bool existed = (skiplist_search(sl, key, tmp) == KV_OK);

    kv_status_t status = skiplist_insert(sl, key, value);
    if (status == KV_OK && !existed) {
        atomic_fetch_add(&hm->total_keys, 1);
    }
    atomic_fetch_add(&hm->total_ops, 1);
    return status;
}

kv_status_t hashmap_get(hashmap_t *hm, const char *key, char *out_value) {
    if (!hm || !key) return KV_ERR_INVALID;

    int idx = bucket_index(key);
    atomic_fetch_add(&hm->total_ops, 1);
    return skiplist_search(hm->buckets[idx], key, out_value);
}

kv_status_t hashmap_del(hashmap_t *hm, const char *key) {
    if (!hm || !key) return KV_ERR_INVALID;

    int idx = bucket_index(key);
    kv_status_t status = skiplist_delete(hm->buckets[idx], key);
    if (status == KV_OK) {
        atomic_fetch_sub(&hm->total_keys, 1);
    }
    atomic_fetch_add(&hm->total_ops, 1);
    return status;
}

long hashmap_key_count(hashmap_t *hm) {
    return hm ? atomic_load(&hm->total_keys) : 0;
}

long hashmap_op_count(hashmap_t *hm) {
    return hm ? atomic_load(&hm->total_ops) : 0;
}
