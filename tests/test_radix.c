/*
 * test_radix.c — Radix Tree Tests
 */
#include "radix_tree.h"

static int passed = 0, failed = 0;
#define TEST(name, cond) do { \
    if (cond) { printf("  [PASS] %s\n", name); passed++; } \
    else { printf("  [FAIL] %s\n", name); failed++; } \
} while(0)

static void test_basic(void) {
    printf("\n--- Test: Radix Tree Basic ---\n");
    radix_tree_t *rt = radix_tree_create();
    TEST("create", rt != NULL);

    TEST("insert user:1", radix_tree_insert(rt, "user:1") == KV_OK);
    TEST("insert user:2", radix_tree_insert(rt, "user:2") == KV_OK);
    TEST("insert user:100", radix_tree_insert(rt, "user:100") == KV_OK);
    TEST("insert admin:1", radix_tree_insert(rt, "admin:1") == KV_OK);
    TEST("count 4", radix_tree_count(rt) == 4);

    TEST("contains user:1", radix_tree_contains(rt, "user:1") == true);
    TEST("contains admin:1", radix_tree_contains(rt, "admin:1") == true);
    TEST("!contains user:3", radix_tree_contains(rt, "user:3") == false);
    TEST("!contains use", radix_tree_contains(rt, "use") == false);

    TEST("delete user:1", radix_tree_delete(rt, "user:1") == KV_OK);
    TEST("!contains deleted", radix_tree_contains(rt, "user:1") == false);
    TEST("count 3", radix_tree_count(rt) == 3);

    radix_tree_destroy(rt);
}

static void test_prefix_search(void) {
    printf("\n--- Test: Prefix Search ---\n");
    radix_tree_t *rt = radix_tree_create();

    radix_tree_insert(rt, "user:alice");
    radix_tree_insert(rt, "user:bob");
    radix_tree_insert(rt, "user:charlie");
    radix_tree_insert(rt, "admin:root");
    radix_tree_insert(rt, "admin:backup");
    radix_tree_insert(rt, "session:abc123");

    char keys[64][KV_MAX_KEY_LEN];

    int count = radix_tree_prefix_search(rt, "user:", keys, 64);
    printf("   Found %d keys with prefix 'user:'\n", count);
    TEST("user: prefix count", count == 3);

    count = radix_tree_prefix_search(rt, "admin:", keys, 64);
    printf("   Found %d keys with prefix 'admin:'\n", count);
    TEST("admin: prefix count", count == 2);

    count = radix_tree_prefix_search(rt, "session:", keys, 64);
    TEST("session: prefix count", count == 1);

    count = radix_tree_prefix_search(rt, "missing:", keys, 64);
    TEST("missing: no results", count == 0);

    /* All keys */
    count = radix_tree_prefix_search(rt, "", keys, 64);
    TEST("all keys", count == 6);

    radix_tree_destroy(rt);
}

static void test_edge_cases(void) {
    printf("\n--- Test: Edge Cases ---\n");
    radix_tree_t *rt = radix_tree_create();

    /* Single character keys */
    TEST("insert 'a'", radix_tree_insert(rt, "a") == KV_OK);
    TEST("insert 'b'", radix_tree_insert(rt, "b") == KV_OK);
    TEST("contains 'a'", radix_tree_contains(rt, "a"));

    /* Overlapping prefixes */
    TEST("insert 'test'", radix_tree_insert(rt, "test") == KV_OK);
    TEST("insert 'testing'", radix_tree_insert(rt, "testing") == KV_OK);
    TEST("insert 'tested'", radix_tree_insert(rt, "tested") == KV_OK);
    TEST("contains 'test'", radix_tree_contains(rt, "test"));
    TEST("contains 'testing'", radix_tree_contains(rt, "testing"));
    TEST("contains 'tested'", radix_tree_contains(rt, "tested"));
    TEST("!contains 'tes'", !radix_tree_contains(rt, "tes"));

    char keys[64][KV_MAX_KEY_LEN];
    int count = radix_tree_prefix_search(rt, "test", keys, 64);
    TEST("prefix 'test' count", count == 3);

    radix_tree_destroy(rt);
}

int main(void) {
    printf("=== Radix Tree Test Suite ===\n");

    test_basic();
    test_prefix_search();
    test_edge_cases();

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
