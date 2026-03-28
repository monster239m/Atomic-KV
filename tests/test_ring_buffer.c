/*
 * test_ring_buffer.c — Lock-Free Ring Buffer Tests
 */
#include "ring_buffer.h"

static int passed = 0, failed = 0;
#define TEST(name, cond) do { \
    if (cond) { printf("  [PASS] %s\n", name); passed++; } \
    else { printf("  [FAIL] %s\n", name); failed++; } \
} while(0)

static void test_basic(void) {
    printf("\n--- Test: Ring Buffer Basic ---\n");
    ring_buffer_t *rb = ring_buffer_create(16);
    TEST("create", rb != NULL);
    TEST("empty", ring_buffer_is_empty(rb));

    kv_command_t cmd = {0};
    cmd.type = CMD_SET;
    strcpy(cmd.key, "hello");
    strcpy(cmd.value, "world");

    TEST("enqueue 1", ring_buffer_enqueue(rb, &cmd) == KV_OK);
    TEST("not empty", !ring_buffer_is_empty(rb));
    TEST("size 1", ring_buffer_size(rb) == 1);

    kv_command_t out = {0};
    TEST("dequeue", ring_buffer_dequeue(rb, &out) == KV_OK);
    TEST("type match", out.type == CMD_SET);
    TEST("key match", strcmp(out.key, "hello") == 0);
    TEST("val match", strcmp(out.value, "world") == 0);
    TEST("empty after", ring_buffer_is_empty(rb));

    ring_buffer_destroy(rb);
}

static void test_fill_capacity(void) {
    printf("\n--- Test: Fill to Capacity ---\n");
    ring_buffer_t *rb = ring_buffer_create(16); /* rounds to 16 */

    kv_command_t cmd = {0};
    cmd.type = CMD_SET;
    int enqueued = 0;

    /* Fill it up */
    for (int i = 0; i < 20; i++) {
        snprintf(cmd.key, sizeof(cmd.key), "k%d", i);
        if (ring_buffer_enqueue(rb, &cmd) == KV_OK) {
            enqueued++;
        }
    }
    printf("   Enqueued: %d (capacity: 16)\n", enqueued);
    TEST("fill count <= capacity", enqueued <= 16);
    TEST("fill count > 0", enqueued > 0);

    /* Drain */
    kv_command_t out;
    int dequeued = 0;
    while (ring_buffer_dequeue(rb, &out) == KV_OK) {
        dequeued++;
    }
    TEST("drain matches", dequeued == enqueued);
    TEST("empty after drain", ring_buffer_is_empty(rb));

    ring_buffer_destroy(rb);
}

/* ---- Multi-producer test ---- */
#define PRODUCER_OPS 5000
#define NUM_PRODUCERS 4

typedef struct {
    ring_buffer_t *rb;
    int tid;
    int enqueued;
} producer_arg_t;

static DWORD WINAPI producer_thread(LPVOID arg) {
    producer_arg_t *pa = (producer_arg_t *)arg;
    kv_command_t cmd = {0};
    cmd.type = CMD_SET;
    pa->enqueued = 0;
    for (int i = 0; i < PRODUCER_OPS; i++) {
        snprintf(cmd.key, sizeof(cmd.key), "t%d:k%d", pa->tid, i);
        if (ring_buffer_enqueue(pa->rb, &cmd) == KV_OK) {
            pa->enqueued++;
        } else {
            /* Buffer full, wait a bit and retry */
            SwitchToThread();
            i--; /* retry */
        }
    }
    return 0;
}

static void test_concurrent_producers(void) {
    printf("\n--- Test: Multi-Producer (%d threads x %d ops) ---\n",
           NUM_PRODUCERS, PRODUCER_OPS);

    ring_buffer_t *rb = ring_buffer_create(KV_RING_BUFFER_SIZE);

    producer_arg_t args[NUM_PRODUCERS];
    HANDLE threads[NUM_PRODUCERS];

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        args[i].rb = rb;
        args[i].tid = i;
        threads[i] = CreateThread(NULL, 0, producer_thread, &args[i], 0, NULL);
    }

    /* Consumer: drain in parallel */
    int consumed = 0;
    kv_command_t out;
    int idle_count = 0;
    int total_expected = NUM_PRODUCERS * PRODUCER_OPS;

    /* Wait for threads to finish, consuming as we go */
    DWORD wait_result;
    do {
        while (ring_buffer_dequeue(rb, &out) == KV_OK) {
            consumed++;
            idle_count = 0;
        }
        wait_result = WaitForMultipleObjects(NUM_PRODUCERS, threads, TRUE, 10);
    } while (wait_result == WAIT_TIMEOUT);

    /* Final drain */
    while (ring_buffer_dequeue(rb, &out) == KV_OK) {
        consumed++;
    }

    for (int i = 0; i < NUM_PRODUCERS; i++) CloseHandle(threads[i]);

    int total_enqueued = 0;
    for (int i = 0; i < NUM_PRODUCERS; i++) total_enqueued += args[i].enqueued;

    printf("   Enqueued: %d, Consumed: %d\n", total_enqueued, consumed);
    TEST("no data loss", consumed == total_enqueued);

    ring_buffer_destroy(rb);
}

int main(void) {
    printf("=== Ring Buffer Test Suite ===\n");

    test_basic();
    test_fill_capacity();
    test_concurrent_producers();

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
