/*
 * test_hashmap.c — Concurrent Hash Map Tests
 */
#include "hashmap.h"

static int passed = 0, failed = 0;
#define TEST(name, cond) do { \
    if (cond) { printf("  [PASS] %s\n", name); passed++; } \
    else { printf("  [FAIL] %s\n", name); failed++; } \
} while(0)

/* ---- Basic operations ---- */
static void test_basic(void) {
    printf("\n--- Test: Hash Map Basic Ops ---\n");
    hashmap_t *hm = hashmap_create();
    TEST("create", hm != NULL);

    TEST("set hello",  hashmap_set(hm, "hello", "world") == KV_OK);
    TEST("set foo",    hashmap_set(hm, "foo", "bar") == KV_OK);
    TEST("key count",  hashmap_key_count(hm) == 2);

    char val[KV_MAX_VALUE_LEN];
    TEST("get hello",  hashmap_get(hm, "hello", val) == KV_OK && strcmp(val,"world")==0);
    TEST("get miss",   hashmap_get(hm, "missing", val) == KV_ERR_NOT_FOUND);

    TEST("del foo",    hashmap_del(hm, "foo") == KV_OK);
    TEST("foo gone",   hashmap_get(hm, "foo", val) == KV_ERR_NOT_FOUND);
    TEST("key count 1", hashmap_key_count(hm) == 1);

    /* Update */
    TEST("update hello", hashmap_set(hm, "hello", "universe") == KV_OK);
    TEST("verify update", hashmap_get(hm, "hello", val) == KV_OK && strcmp(val,"universe")==0);
    TEST("key count still 1", hashmap_key_count(hm) == 1);

    hashmap_destroy(hm);
}

/* ---- Concurrent SET with unique keys ---- */
#define THREAD_OPS 5000
#define NUM_THREADS 4

typedef struct { hashmap_t *hm; int tid; } targ_t;

static DWORD WINAPI concurrent_set(LPVOID arg) {
    targ_t *t = (targ_t *)arg;
    char key[64], val[64];
    for (int i = 0; i < THREAD_OPS; i++) {
        snprintf(key, sizeof(key), "t%d:k%d", t->tid, i);
        snprintf(val, sizeof(val), "v%d", i);
        hashmap_set(t->hm, key, val);
    }
    return 0;
}

static void test_concurrent(void) {
    printf("\n--- Test: Concurrent SET (%d threads x %d ops) ---\n",
           NUM_THREADS, THREAD_OPS);
    hashmap_t *hm = hashmap_create();

    HANDLE threads[NUM_THREADS];
    targ_t args[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].hm = hm;
        args[i].tid = i;
        threads[i] = CreateThread(NULL, 0, concurrent_set, &args[i], 0, NULL);
    }
    WaitForMultipleObjects(NUM_THREADS, threads, TRUE, INFINITE);
    for (int i = 0; i < NUM_THREADS; i++) CloseHandle(threads[i]);

    long count = hashmap_key_count(hm);
    printf("   Keys: %ld (expected %d)\n", count, NUM_THREADS * THREAD_OPS);
    TEST("all keys present", count == NUM_THREADS * THREAD_OPS);

    /* Verify samples */
    char val[KV_MAX_VALUE_LEN];
    TEST("sample t0:k0", hashmap_get(hm, "t0:k0", val) == KV_OK);
    TEST("sample t3:k4999", hashmap_get(hm, "t3:k4999", val) == KV_OK);

    hashmap_destroy(hm);
}

/* ---- Concurrent SET on same key ---- */
static DWORD WINAPI concurrent_same_key(LPVOID arg) {
    targ_t *t = (targ_t *)arg;
    char val[64];
    for (int i = 0; i < THREAD_OPS; i++) {
        snprintf(val, sizeof(val), "t%d-v%d", t->tid, i);
        hashmap_set(t->hm, "contended_key", val);
    }
    return 0;
}

static void test_contended(void) {
    printf("\n--- Test: Contended Key (%d threads x %d writes to same key) ---\n",
           NUM_THREADS, THREAD_OPS);
    hashmap_t *hm = hashmap_create();
    hashmap_set(hm, "contended_key", "initial");

    HANDLE threads[NUM_THREADS];
    targ_t args[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].hm = hm;
        args[i].tid = i;
        threads[i] = CreateThread(NULL, 0, concurrent_same_key, &args[i], 0, NULL);
    }
    WaitForMultipleObjects(NUM_THREADS, threads, TRUE, INFINITE);
    for (int i = 0; i < NUM_THREADS; i++) CloseHandle(threads[i]);

    char val[KV_MAX_VALUE_LEN];
    kv_status_t s = hashmap_get(hm, "contended_key", val);
    printf("   Final value: %s\n", val);
    TEST("no crash", s == KV_OK);
    TEST("key count 1", hashmap_key_count(hm) == 1);

    hashmap_destroy(hm);
}

int main(void) {
    printf("=== Hash Map Test Suite ===\n");
    srand((unsigned int)time(NULL));

    test_basic();
    test_concurrent();
    test_contended();

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
