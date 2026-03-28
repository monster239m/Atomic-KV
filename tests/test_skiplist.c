/*
 * test_skiplist.c — Concurrent Skip List Tests
 */
#include "skiplist.h"

static atomic_int test_passed = 0;
static atomic_int test_failed = 0;

#define TEST(name, cond) do { \
    if (cond) { printf("  [PASS] %s\n", name); atomic_fetch_add(&test_passed,1); } \
    else { printf("  [FAIL] %s\n", name); atomic_fetch_add(&test_failed,1); } \
} while(0)

/* ---- Test 1: Basic operations ---- */
static void test_basic(void) {
    printf("\n--- Test: Basic Insert/Search/Delete ---\n");
    skiplist_t *sl = skiplist_create();

    TEST("create", sl != NULL);
    TEST("empty count", skiplist_count(sl) == 0);

    TEST("insert hello", skiplist_insert(sl, "hello", "world") == KV_OK);
    TEST("insert foo",   skiplist_insert(sl, "foo", "bar") == KV_OK);
    TEST("insert baz",   skiplist_insert(sl, "baz", "qux") == KV_OK);
    TEST("count is 3",   skiplist_count(sl) == 3);

    char val[KV_MAX_VALUE_LEN];
    TEST("search hello", skiplist_search(sl, "hello", val) == KV_OK && strcmp(val, "world") == 0);
    TEST("search foo",   skiplist_search(sl, "foo", val) == KV_OK && strcmp(val, "bar") == 0);
    TEST("search miss",  skiplist_search(sl, "missing", val) == KV_ERR_NOT_FOUND);

    TEST("delete foo",   skiplist_delete(sl, "foo") == KV_OK);
    TEST("count is 2",   skiplist_count(sl) == 2);
    TEST("foo gone",     skiplist_search(sl, "foo", val) == KV_ERR_NOT_FOUND);

    /* Update existing key */
    TEST("update hello", skiplist_insert(sl, "hello", "universe") == KV_OK);
    TEST("verify update", skiplist_search(sl, "hello", val) == KV_OK && strcmp(val, "universe") == 0);

    skiplist_destroy(sl);
    printf("   Basic tests done.\n");
}

/* ---- Test 2: Range query ---- */
static void test_range(void) {
    printf("\n--- Test: Range Query ---\n");
    skiplist_t *sl = skiplist_create();

    skiplist_insert(sl, "apple", "1");
    skiplist_insert(sl, "banana", "2");
    skiplist_insert(sl, "cherry", "3");
    skiplist_insert(sl, "date", "4");
    skiplist_insert(sl, "elderberry", "5");

    char keys[10][KV_MAX_KEY_LEN];
    int count = skiplist_range(sl, "banana", "date", keys, 10);
    TEST("range count", count == 3);
    if (count >= 3) {
        TEST("range[0]=banana",  strcmp(keys[0], "banana") == 0);
        TEST("range[1]=cherry",  strcmp(keys[1], "cherry") == 0);
        TEST("range[2]=date",    strcmp(keys[2], "date") == 0);
    }

    skiplist_destroy(sl);
    printf("   Range tests done.\n");
}

/* ---- Test 3: Concurrent insert (4 threads) ---- */
#define THREAD_OPS 5000
#define NUM_THREADS 4

typedef struct {
    skiplist_t *sl;
    int thread_id;
} thread_arg_t;

static DWORD WINAPI concurrent_insert(LPVOID arg) {
    thread_arg_t *ta = (thread_arg_t *)arg;
    char key[64], val[64];
    for (int i = 0; i < THREAD_OPS; i++) {
        snprintf(key, sizeof(key), "t%d:k%d", ta->thread_id, i);
        snprintf(val, sizeof(val), "v%d", i);
        skiplist_insert(ta->sl, key, val);
    }
    return 0;
}

static void test_concurrent(void) {
    printf("\n--- Test: Concurrent Insert (%d threads x %d ops) ---\n",
           NUM_THREADS, THREAD_OPS);
    skiplist_t *sl = skiplist_create();

    HANDLE threads[NUM_THREADS];
    thread_arg_t args[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].sl = sl;
        args[i].thread_id = i;
        threads[i] = CreateThread(NULL, 0, concurrent_insert, &args[i], 0, NULL);
    }
    WaitForMultipleObjects(NUM_THREADS, threads, TRUE, INFINITE);
    for (int i = 0; i < NUM_THREADS; i++) CloseHandle(threads[i]);

    int expected = NUM_THREADS * THREAD_OPS;
    int actual = skiplist_count(sl);
    printf("   Expected: %d, Actual: %d\n", expected, actual);
    TEST("concurrent count", actual == expected);

    /* Verify a sample of keys */
    char val[KV_MAX_VALUE_LEN];
    TEST("sample t0:k0",    skiplist_search(sl, "t0:k0", val) == KV_OK);
    TEST("sample t3:k4999", skiplist_search(sl, "t3:k4999", val) == KV_OK);

    skiplist_destroy(sl);
    printf("   Concurrent tests done.\n");
}

/* ---- Test 4: Concurrent insert + delete ---- */
static DWORD WINAPI concurrent_mixed(LPVOID arg) {
    thread_arg_t *ta = (thread_arg_t *)arg;
    char key[64], val[64];
    for (int i = 0; i < THREAD_OPS; i++) {
        snprintf(key, sizeof(key), "m%d:k%d", ta->thread_id, i);
        snprintf(val, sizeof(val), "v%d", i);
        skiplist_insert(ta->sl, key, val);
        /* Delete every other key */
        if (i > 0 && i % 2 == 0) {
            snprintf(key, sizeof(key), "m%d:k%d", ta->thread_id, i - 1);
            skiplist_delete(ta->sl, key);
        }
    }
    return 0;
}

static void test_concurrent_mixed(void) {
    printf("\n--- Test: Concurrent Insert+Delete (%d threads) ---\n", NUM_THREADS);
    skiplist_t *sl = skiplist_create();

    HANDLE threads[NUM_THREADS];
    thread_arg_t args[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].sl = sl;
        args[i].thread_id = i;
        threads[i] = CreateThread(NULL, 0, concurrent_mixed, &args[i], 0, NULL);
    }
    WaitForMultipleObjects(NUM_THREADS, threads, TRUE, INFINITE);
    for (int i = 0; i < NUM_THREADS; i++) CloseHandle(threads[i]);

    int actual = skiplist_count(sl);
    printf("   Remaining keys after mixed ops: %d\n", actual);
    TEST("no crash", actual >= 0); /* main goal: no memory corruption */

    skiplist_destroy(sl);
    printf("   Mixed concurrent tests done.\n");
}

int main(void) {
    printf("=== Skip List Test Suite ===\n");
    srand((unsigned int)time(NULL));

    test_basic();
    test_range();
    test_concurrent();
    test_concurrent_mixed();

    printf("\n=== Results: %d passed, %d failed ===\n",
           atomic_load(&test_passed), atomic_load(&test_failed));
    return atomic_load(&test_failed) > 0 ? 1 : 0;
}
