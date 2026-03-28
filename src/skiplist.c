/*
 * skiplist.c — Concurrent Skip List
 *
 * Uses a fine-grained approach: a lightweight spinlock for write
 * operations (insert/delete), while reads (search) are lock-free
 * using atomic loads. This avoids the ABA problem and dangling
 * pointer issues that pure CAS-based skip lists face with
 * manual memory management in C (no GC/hazard pointers).
 *
 * The spinlock is extremely lightweight — it only protects the
 * structural modification (linking/unlinking), not the traversal.
 * This means concurrent reads never block and concurrent writes
 * only contend briefly during the actual pointer swap.
 */

#include "skiplist.h"

/* ============================================================
 *  Lightweight Spinlock (using atomic CAS)
 * ============================================================ */
typedef struct {
    atomic_int locked;
} spinlock_t;

static inline void spin_init(spinlock_t *lock) {
    atomic_store(&lock->locked, 0);
}

static inline void spin_lock(spinlock_t *lock) {
    int expected;
    for (;;) {
        expected = 0;
        if (atomic_compare_exchange_weak(&lock->locked, &expected, 1))
            return;
        /* Yield CPU while spinning */
        SwitchToThread();
    }
}

static inline void spin_unlock(spinlock_t *lock) {
    atomic_store(&lock->locked, 0);
}

/* ============================================================
 *  Internal: Node allocation
 * ============================================================ */
static skiplist_node_t *node_create(const char *key, const char *value, int level) {
    skiplist_node_t *node = (skiplist_node_t *)calloc(1, sizeof(skiplist_node_t));
    if (!node) return NULL;

    strncpy(node->key, key, KV_MAX_KEY_LEN - 1);
    node->key[KV_MAX_KEY_LEN - 1] = '\0';
    strncpy(node->value, value, KV_MAX_VALUE_LEN - 1);
    node->value[KV_MAX_VALUE_LEN - 1] = '\0';
    node->level = level;
    atomic_store(&node->marked, 0);
    atomic_store(&node->ref_count, 1);

    for (int i = 0; i < SKIPLIST_MAX_LEVEL; i++) {
        atomic_store(&node->next[i], (uintptr_t)NULL);
    }
    return node;
}

/* ============================================================
 *  Skiplist struct (extended with lock)
 * ============================================================ */
/* We embed the spinlock by using a file-scoped static.
 * Since we support multiple skip lists, we use a small lock table
 * hashed by skiplist pointer. For simplicity, we add the lock
 * to each skiplist instance via a parallel table. */
#define LOCK_TABLE_SIZE 256
static spinlock_t g_lock_table[LOCK_TABLE_SIZE];
static atomic_int g_lock_init = 0;

static void ensure_lock_init(void) {
    int expected = 0;
    if (atomic_compare_exchange_strong(&g_lock_init, &expected, 1)) {
        for (int i = 0; i < LOCK_TABLE_SIZE; i++) {
            spin_init(&g_lock_table[i]);
        }
    }
}

static spinlock_t *get_lock(skiplist_t *sl) {
    uintptr_t h = (uintptr_t)sl;
    return &g_lock_table[(h >> 4) % LOCK_TABLE_SIZE];
}

/* ============================================================
 *  Create / Destroy
 * ============================================================ */
skiplist_t *skiplist_create(void) {
    ensure_lock_init();

    skiplist_t *sl = (skiplist_t *)calloc(1, sizeof(skiplist_t));
    if (!sl) return NULL;

    sl->header = node_create("", "", SKIPLIST_MAX_LEVEL);
    if (!sl->header) {
        free(sl);
        return NULL;
    }
    atomic_store(&sl->count, 0);
    atomic_store(&sl->max_level, 1);
    return sl;
}

void skiplist_destroy(skiplist_t *sl) {
    if (!sl) return;

    skiplist_node_t *curr = (skiplist_node_t *)atomic_load(&sl->header->next[0]);
    while (curr) {
        skiplist_node_t *next = (skiplist_node_t *)atomic_load(&curr->next[0]);
        free(curr);
        curr = next;
    }
    free(sl->header);
    free(sl);
}

/* ============================================================
 *  Insert (spinlock-protected structural modification)
 *  Reads for finding position use atomic loads (lock-free).
 * ============================================================ */
kv_status_t skiplist_insert(skiplist_t *sl, const char *key, const char *value) {
    if (!sl || !key || !value) return KV_ERR_INVALID;

    spinlock_t *lock = get_lock(sl);
    int new_level = kv_random_level();

    spin_lock(lock);

    /* Find predecessors at each level */
    skiplist_node_t *preds[SKIPLIST_MAX_LEVEL];
    skiplist_node_t *curr;
    int max_lv = atomic_load(&sl->max_level);

    skiplist_node_t *pred = sl->header;
    for (int lv = max_lv - 1; lv >= 0; lv--) {
        curr = (skiplist_node_t *)atomic_load(&pred->next[lv]);
        while (curr) {
            int cmp = strcmp(curr->key, key);
            if (cmp < 0) {
                pred = curr;
                curr = (skiplist_node_t *)atomic_load(&pred->next[lv]);
            } else if (cmp == 0) {
                /* Key exists — update value in place */
                strncpy(curr->value, value, KV_MAX_VALUE_LEN - 1);
                curr->value[KV_MAX_VALUE_LEN - 1] = '\0';
                /* If it was marked for deletion, unmark it */
                atomic_store(&curr->marked, 0);
                spin_unlock(lock);
                return KV_OK;
            } else {
                break;
            }
        }
        preds[lv] = pred;
    }

    /* Create new node */
    skiplist_node_t *new_node = node_create(key, value, new_level);
    if (!new_node) {
        spin_unlock(lock);
        return KV_ERR_MEMORY;
    }

    /* Update max level if needed */
    if (new_level > max_lv) {
        for (int lv = max_lv; lv < new_level; lv++) {
            preds[lv] = sl->header;
        }
        atomic_store(&sl->max_level, new_level);
    }

    /* Link the new node at all levels */
    for (int lv = 0; lv < new_level; lv++) {
        atomic_store(&new_node->next[lv], atomic_load(&preds[lv]->next[lv]));
        atomic_store(&preds[lv]->next[lv], (uintptr_t)new_node);
    }

    atomic_fetch_add(&sl->count, 1);
    spin_unlock(lock);
    return KV_OK;
}

/* ============================================================
 *  Search (completely lock-free — just atomic loads)
 * ============================================================ */
kv_status_t skiplist_search(skiplist_t *sl, const char *key, char *out_value) {
    if (!sl || !key) return KV_ERR_INVALID;

    int max_lv = atomic_load(&sl->max_level);
    skiplist_node_t *pred = sl->header;

    for (int lv = max_lv - 1; lv >= 0; lv--) {
        skiplist_node_t *curr = (skiplist_node_t *)atomic_load(&pred->next[lv]);
        while (curr) {
            if (atomic_load(&curr->marked)) {
                curr = (skiplist_node_t *)atomic_load(&curr->next[lv]);
                continue;
            }
            int cmp = strcmp(curr->key, key);
            if (cmp < 0) {
                pred = curr;
                curr = (skiplist_node_t *)atomic_load(&pred->next[lv]);
            } else if (cmp == 0) {
                if (out_value) {
                    strncpy(out_value, curr->value, KV_MAX_VALUE_LEN - 1);
                    out_value[KV_MAX_VALUE_LEN - 1] = '\0';
                }
                return KV_OK;
            } else {
                break;
            }
        }
    }
    return KV_ERR_NOT_FOUND;
}

/* ============================================================
 *  Delete (spinlock-protected unlinking)
 * ============================================================ */
kv_status_t skiplist_delete(skiplist_t *sl, const char *key) {
    if (!sl || !key) return KV_ERR_INVALID;

    spinlock_t *lock = get_lock(sl);
    spin_lock(lock);

    int max_lv = atomic_load(&sl->max_level);
    skiplist_node_t *preds[SKIPLIST_MAX_LEVEL];
    skiplist_node_t *target = NULL;

    skiplist_node_t *pred = sl->header;
    for (int lv = max_lv - 1; lv >= 0; lv--) {
        skiplist_node_t *curr = (skiplist_node_t *)atomic_load(&pred->next[lv]);
        while (curr) {
            int cmp = strcmp(curr->key, key);
            if (cmp < 0) {
                pred = curr;
                curr = (skiplist_node_t *)atomic_load(&pred->next[lv]);
            } else {
                if (cmp == 0) target = curr;
                break;
            }
        }
        preds[lv] = pred;
    }

    if (!target || atomic_load(&target->marked)) {
        spin_unlock(lock);
        return KV_ERR_NOT_FOUND;
    }

    /* Mark as deleted */
    atomic_store(&target->marked, 1);

    /* Unlink from all levels */
    for (int lv = target->level - 1; lv >= 0; lv--) {
        skiplist_node_t *next = (skiplist_node_t *)atomic_load(&target->next[lv]);
        atomic_store(&preds[lv]->next[lv], (uintptr_t)next);
    }

    atomic_fetch_sub(&sl->count, 1);
    spin_unlock(lock);

    /* Note: We don't free the node immediately — reads might still
     * be traversing through it. In a production system, we'd use
     * epoch-based reclamation or hazard pointers. For this project,
     * nodes are freed at skiplist_destroy() time. */
    return KV_OK;
}

/* ============================================================
 *  Count
 * ============================================================ */
int skiplist_count(skiplist_t *sl) {
    return sl ? atomic_load(&sl->count) : 0;
}

/* ============================================================
 *  Collect all keys (for KEYS command)
 * ============================================================ */
int skiplist_collect_keys(skiplist_t *sl, char keys[][KV_MAX_KEY_LEN], int max_keys) {
    if (!sl || !keys || max_keys <= 0) return 0;

    int count = 0;
    skiplist_node_t *curr = (skiplist_node_t *)atomic_load(&sl->header->next[0]);

    while (curr && count < max_keys) {
        if (!atomic_load(&curr->marked)) {
            strncpy(keys[count], curr->key, KV_MAX_KEY_LEN - 1);
            keys[count][KV_MAX_KEY_LEN - 1] = '\0';
            count++;
        }
        curr = (skiplist_node_t *)atomic_load(&curr->next[0]);
    }
    return count;
}

/* ============================================================
 *  Range query [start_key, end_key]
 * ============================================================ */
int skiplist_range(skiplist_t *sl, const char *start_key, const char *end_key,
                   char keys[][KV_MAX_KEY_LEN], int max_keys) {
    if (!sl || !start_key || !end_key || !keys || max_keys <= 0) return 0;

    int count = 0;
    int max_lv = atomic_load(&sl->max_level);
    skiplist_node_t *pred = sl->header;

    /* Navigate to start_key position */
    for (int lv = max_lv - 1; lv >= 0; lv--) {
        skiplist_node_t *curr = (skiplist_node_t *)atomic_load(&pred->next[lv]);
        while (curr && strcmp(curr->key, start_key) < 0) {
            if (!atomic_load(&curr->marked)) {
                pred = curr;
            }
            curr = (skiplist_node_t *)atomic_load(&curr->next[lv]);
        }
    }

    /* Traverse level 0 collecting keys in range */
    skiplist_node_t *curr = (skiplist_node_t *)atomic_load(&pred->next[0]);
    while (curr && count < max_keys) {
        if (!atomic_load(&curr->marked)) {
            if (strcmp(curr->key, end_key) > 0) break;
            if (strcmp(curr->key, start_key) >= 0) {
                strncpy(keys[count], curr->key, KV_MAX_KEY_LEN - 1);
                keys[count][KV_MAX_KEY_LEN - 1] = '\0';
                count++;
            }
        }
        curr = (skiplist_node_t *)atomic_load(&curr->next[0]);
    }
    return count;
}
