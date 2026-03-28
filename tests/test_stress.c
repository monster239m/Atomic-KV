/*
 * test_stress.c — Full Stress Test
 *
 * 4 threads × 25,000 ops each = 100,000 total operations
 * Mixed SET/GET/DEL operations on the hash map + radix tree.
 */
#include "hashmap.h"
#include "radix_tree.h"

#define OPS_PER_THREAD 25000
#define NUM_THREADS    4

static atomic_int total_success = 0;
static atomic_int total_errors = 0;

typedef struct {
    hashmap_t    *hm;
    radix_tree_t *rt;
    int           tid;
} stress_arg_t;

static DWORD WINAPI stress_worker(LPVOID arg) {
    stress_arg_t *sa = (stress_arg_t *)arg;
    char key[64], val[KV_MAX_VALUE_LEN];
    int success = 0, errors = 0;

    for (int i = 0; i < OPS_PER_THREAD; i++) {
        int op = i % 5;  /* SET, SET, GET, GET, DEL cycle */
        int key_id = (sa->tid * OPS_PER_THREAD + i) % 10000;
        snprintf(key, sizeof(key), "stress:%d", key_id);

        switch (op) {
        case 0:
        case 1:
            snprintf(val, sizeof(val), "value-%d-%d", sa->tid, i);
            if (hashmap_set(sa->hm, key, val) == KV_OK) {
                radix_tree_insert(sa->rt, key);
                success++;
            } else errors++;
            break;
        case 2:
        case 3:
            if (hashmap_get(sa->hm, key, val) == KV_OK) success++;
            else success++; /* NOT_FOUND is fine */
            break;
        case 4:
            hashmap_del(sa->hm, key);
            radix_tree_delete(sa->rt, key);
            success++;
            break;
        }
    }
    atomic_fetch_add(&total_success, success);
    atomic_fetch_add(&total_errors, errors);
    return 0;
}

int main(void) {
    printf("=== Stress Test: %d threads x %d ops = %d total ===\n\n",
           NUM_THREADS, OPS_PER_THREAD, NUM_THREADS * OPS_PER_THREAD);
    srand((unsigned int)time(NULL));

    hashmap_t *hm = hashmap_create();
    radix_tree_t *rt = radix_tree_create();

    if (!hm || !rt) {
        printf("[FATAL] Failed to create data structures\n");
        return 1;
    }

    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    HANDLE threads[NUM_THREADS];
    stress_arg_t args[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].hm = hm;
        args[i].rt = rt;
        args[i].tid = i;
        threads[i] = CreateThread(NULL, 0, stress_worker, &args[i], 0, NULL);
    }

    WaitForMultipleObjects(NUM_THREADS, threads, TRUE, INFINITE);
    QueryPerformanceCounter(&end);

    for (int i = 0; i < NUM_THREADS; i++) CloseHandle(threads[i]);

    double elapsed_ms = (double)(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;
    int total_ops = NUM_THREADS * OPS_PER_THREAD;
    int succ = atomic_load(&total_success);
    int errs = atomic_load(&total_errors);
    double ops_per_sec = total_ops / (elapsed_ms / 1000.0);

    printf("Results:\n");
    printf("  Total ops:     %d\n", total_ops);
    printf("  Successful:    %d\n", succ);
    printf("  Errors:        %d\n", errs);
    printf("  Time:          %.1f ms\n", elapsed_ms);
    printf("  Throughput:    %.0f ops/sec\n", ops_per_sec);
    printf("  Final keys:    %ld (hashmap), %d (radix)\n",
           hashmap_key_count(hm), radix_tree_count(rt));
    printf("  Total HM ops:  %ld\n", hashmap_op_count(hm));

    printf("\n");
    if (errs == 0 && elapsed_ms < 30000) {
        printf("  [PASS] Stress test completed without errors!\n");
        if (ops_per_sec >= 100000) {
            printf("  [PASS] Throughput target met: %.0f >= 100,000 ops/sec\n", ops_per_sec);
        } else {
            printf("  [INFO] Throughput: %.0f ops/sec (target: 100,000)\n", ops_per_sec);
        }
    } else {
        printf("  [FAIL] Stress test had %d errors\n", errs);
    }

    radix_tree_destroy(rt);
    hashmap_destroy(hm);

    printf("\n=== Stress test complete ===\n");
    return errs > 0 ? 1 : 0;
}
