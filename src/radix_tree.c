/*
 * radix_tree.c — Compressed Radix Tree (Patricia Trie)
 *
 * Provides O(k) key lookup and prefix-based range queries.
 * Uses CRITICAL_SECTION for write operations; reads are lock-free.
 */

#include "radix_tree.h"
#include <string.h>

/* MinGW strdup compatibility */
extern char *strdup(const char *s);

/* ============================================================
 *  Internal: Node creation / destruction
 * ============================================================ */
static radix_node_t *radix_node_create(const char *prefix) {
    radix_node_t *node = (radix_node_t *)calloc(1, sizeof(radix_node_t));
    if (!node) return NULL;

    if (prefix && *prefix) {
        node->prefix = strdup(prefix);
        if (!node->prefix) {
            free(node);
            return NULL;
        }
    } else {
        node->prefix = strdup("");
    }

    node->value = NULL;
    node->has_value = false;
    node->full_key[0] = '\0';
    node->child_count = 0;
    atomic_store(&node->ref_count, 1);
    memset(node->children, 0, sizeof(node->children));
    return node;
}

static void radix_node_destroy(radix_node_t *node) {
    if (!node) return;
    for (int i = 0; i < 256; i++) {
        if (node->children[i]) {
            radix_node_destroy(node->children[i]);
        }
    }
    if (node->prefix) free(node->prefix);
    if (node->value) free(node->value);
    free(node);
}

/* ============================================================
 *  Create / Destroy
 * ============================================================ */
radix_tree_t *radix_tree_create(void) {
    radix_tree_t *rt = (radix_tree_t *)calloc(1, sizeof(radix_tree_t));
    if (!rt) return NULL;

    rt->root = radix_node_create("");
    if (!rt->root) {
        free(rt);
        return NULL;
    }
    atomic_store(&rt->count, 0);
    InitializeCriticalSection(&rt->lock);
    return rt;
}

void radix_tree_destroy(radix_tree_t *rt) {
    if (!rt) return;
    if (rt->root) radix_node_destroy(rt->root);
    DeleteCriticalSection(&rt->lock);
    free(rt);
}

/* ============================================================
 *  Internal: Find common prefix length between two strings
 * ============================================================ */
static int common_prefix_len(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return i;
}

/* ============================================================
 *  Insert
 * ============================================================ */
kv_status_t radix_tree_insert(radix_tree_t *rt, const char *key) {
    if (!rt || !key || !*key) return KV_ERR_INVALID;

    EnterCriticalSection(&rt->lock);

    radix_node_t *node = rt->root;
    const char *remaining = key;

    while (*remaining) {
        unsigned char idx = (unsigned char)*remaining;
        radix_node_t *child = node->children[idx];

        if (!child) {
            /* No child with this first char — create leaf node */
            radix_node_t *new_node = radix_node_create(remaining);
            if (!new_node) {
                LeaveCriticalSection(&rt->lock);
                return KV_ERR_MEMORY;
            }
            new_node->has_value = true;
            strncpy(new_node->full_key, key, KV_MAX_KEY_LEN - 1);
            new_node->full_key[KV_MAX_KEY_LEN - 1] = '\0';
            node->children[idx] = new_node;
            node->child_count++;
            atomic_fetch_add(&rt->count, 1);
            LeaveCriticalSection(&rt->lock);
            return KV_OK;
        }

        /* Child exists — compare prefixes */
        int common = common_prefix_len(child->prefix, remaining);
        int child_prefix_len = (int)strlen(child->prefix);

        if (common == child_prefix_len) {
            /* Full match of child's prefix — descend */
            remaining += common;
            if (*remaining == '\0') {
                /* Exact match to this node */
                if (!child->has_value) {
                    child->has_value = true;
                    strncpy(child->full_key, key, KV_MAX_KEY_LEN - 1);
                    child->full_key[KV_MAX_KEY_LEN - 1] = '\0';
                    atomic_fetch_add(&rt->count, 1);
                }
                LeaveCriticalSection(&rt->lock);
                return KV_OK;
            }
            node = child;
            continue;
        }

        /* Partial match — need to split the child node */
        /* Create a new internal node with the common prefix */
        char *common_str = (char *)malloc(common + 1);
        if (!common_str) {
            LeaveCriticalSection(&rt->lock);
            return KV_ERR_MEMORY;
        }
        strncpy(common_str, child->prefix, common);
        common_str[common] = '\0';

        radix_node_t *split_node = radix_node_create(common_str);
        free(common_str);
        if (!split_node) {
            LeaveCriticalSection(&rt->lock);
            return KV_ERR_MEMORY;
        }

        /* Move old child under split_node with remaining suffix */
        char *old_suffix = strdup(child->prefix + common);
        if (!old_suffix) {
            free(split_node->prefix);
            free(split_node);
            LeaveCriticalSection(&rt->lock);
            return KV_ERR_MEMORY;
        }
        free(child->prefix);
        child->prefix = old_suffix;

        unsigned char split_idx = (unsigned char)child->prefix[0];
        split_node->children[split_idx] = child;
        split_node->child_count = 1;

        /* Replace child in parent */
        node->children[idx] = split_node;

        remaining += common;
        if (*remaining == '\0') {
            /* The new key ends at the split point */
            split_node->has_value = true;
            strncpy(split_node->full_key, key, KV_MAX_KEY_LEN - 1);
            split_node->full_key[KV_MAX_KEY_LEN - 1] = '\0';
            atomic_fetch_add(&rt->count, 1);
        } else {
            /* Create new leaf for remaining part */
            radix_node_t *new_leaf = radix_node_create(remaining);
            if (!new_leaf) {
                LeaveCriticalSection(&rt->lock);
                return KV_ERR_MEMORY;
            }
            new_leaf->has_value = true;
            strncpy(new_leaf->full_key, key, KV_MAX_KEY_LEN - 1);
            new_leaf->full_key[KV_MAX_KEY_LEN - 1] = '\0';

            unsigned char new_idx = (unsigned char)*remaining;
            split_node->children[new_idx] = new_leaf;
            split_node->child_count++;
            atomic_fetch_add(&rt->count, 1);
        }

        LeaveCriticalSection(&rt->lock);
        return KV_OK;
    }

    LeaveCriticalSection(&rt->lock);
    return KV_OK;
}

/* ============================================================
 *  Delete
 * ============================================================ */
kv_status_t radix_tree_delete(radix_tree_t *rt, const char *key) {
    if (!rt || !key || !*key) return KV_ERR_INVALID;

    EnterCriticalSection(&rt->lock);

    radix_node_t *node = rt->root;
    const char *remaining = key;

    while (*remaining) {
        unsigned char idx = (unsigned char)*remaining;
        radix_node_t *child = node->children[idx];
        if (!child) {
            LeaveCriticalSection(&rt->lock);
            return KV_ERR_NOT_FOUND;
        }

        int common = common_prefix_len(child->prefix, remaining);
        int child_prefix_len = (int)strlen(child->prefix);

        if (common < child_prefix_len) {
            LeaveCriticalSection(&rt->lock);
            return KV_ERR_NOT_FOUND;
        }

        remaining += common;
        if (*remaining == '\0') {
            if (child->has_value) {
                child->has_value = false;
                child->full_key[0] = '\0';
                atomic_fetch_sub(&rt->count, 1);
                LeaveCriticalSection(&rt->lock);
                return KV_OK;
            }
            LeaveCriticalSection(&rt->lock);
            return KV_ERR_NOT_FOUND;
        }
        node = child;
    }

    LeaveCriticalSection(&rt->lock);
    return KV_ERR_NOT_FOUND;
}

/* ============================================================
 *  Contains (lock-free read)
 * ============================================================ */
bool radix_tree_contains(radix_tree_t *rt, const char *key) {
    if (!rt || !key || !*key) return false;

    radix_node_t *node = rt->root;
    const char *remaining = key;

    while (*remaining) {
        unsigned char idx = (unsigned char)*remaining;
        radix_node_t *child = node->children[idx];
        if (!child) return false;

        int common = common_prefix_len(child->prefix, remaining);
        int child_prefix_len = (int)strlen(child->prefix);

        if (common < child_prefix_len) return false;

        remaining += common;
        if (*remaining == '\0') {
            return child->has_value;
        }
        node = child;
    }
    return false;
}

/* ============================================================
 *  Internal: Collect keys recursively from a subtree
 * ============================================================ */
static int collect_keys_recursive(radix_node_t *node,
                                   char keys[][KV_MAX_KEY_LEN],
                                   int max_keys, int count) {
    if (!node || count >= max_keys) return count;

    if (node->has_value && node->full_key[0]) {
        strncpy(keys[count], node->full_key, KV_MAX_KEY_LEN - 1);
        keys[count][KV_MAX_KEY_LEN - 1] = '\0';
        count++;
    }

    for (int i = 0; i < 256 && count < max_keys; i++) {
        if (node->children[i]) {
            count = collect_keys_recursive(node->children[i], keys, max_keys, count);
        }
    }
    return count;
}

/* ============================================================
 *  Prefix Search (KEYS prefix*)
 * ============================================================ */
int radix_tree_prefix_search(radix_tree_t *rt, const char *prefix,
                              char keys[][KV_MAX_KEY_LEN], int max_keys) {
    if (!rt || !prefix || !keys || max_keys <= 0) return 0;

    /* Empty prefix = return all keys */
    if (!*prefix) {
        return collect_keys_recursive(rt->root, keys, max_keys, 0);
    }

    radix_node_t *node = rt->root;
    const char *remaining = prefix;

    while (*remaining) {
        unsigned char idx = (unsigned char)*remaining;
        radix_node_t *child = node->children[idx];
        if (!child) return 0;

        int common = common_prefix_len(child->prefix, remaining);
        int remaining_len = (int)strlen(remaining);
        int child_prefix_len = (int)strlen(child->prefix);

        if (common >= remaining_len) {
            /* prefix is fully consumed — collect everything under this child */
            return collect_keys_recursive(child, keys, max_keys, 0);
        }

        if (common < child_prefix_len) {
            /* Mismatch in the middle of child's prefix */
            return 0;
        }

        remaining += common;
        node = child;
    }

    return collect_keys_recursive(node, keys, max_keys, 0);
}

int radix_tree_count(radix_tree_t *rt) {
    return rt ? atomic_load(&rt->count) : 0;
}
