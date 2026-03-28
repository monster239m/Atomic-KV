/*
 * ring_buffer.c — Lock-Free MPSC Ring Buffer
 *
 * Multiple producers use CAS on the head to claim slots.
 * Single consumer reads from the tail.
 * Power-of-2 capacity for bitmask-based modulo.
 */

#include "ring_buffer.h"

/* Round up to next power of 2 */
static uint32_t next_pow2(uint32_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

ring_buffer_t *ring_buffer_create(uint32_t capacity) {
    ring_buffer_t *rb = (ring_buffer_t *)calloc(1, sizeof(ring_buffer_t));
    if (!rb) return NULL;

    rb->capacity = next_pow2(capacity < 16 ? 16 : capacity);
    rb->mask = rb->capacity - 1;

    rb->buffer = (kv_command_t *)calloc(rb->capacity, sizeof(kv_command_t));
    rb->slot_state = (atomic_int *)calloc(rb->capacity, sizeof(atomic_int));

    if (!rb->buffer || !rb->slot_state) {
        free(rb->buffer);
        free(rb->slot_state);
        free(rb);
        return NULL;
    }

    /* Initialize all slots as empty (0) */
    for (uint32_t i = 0; i < rb->capacity; i++) {
        atomic_store(&rb->slot_state[i], 0);
    }

    atomic_store(&rb->head, 0);
    atomic_store(&rb->tail, 0);
    return rb;
}

void ring_buffer_destroy(ring_buffer_t *rb) {
    if (!rb) return;
    free(rb->buffer);
    free(rb->slot_state);
    free(rb);
}

/*
 * Enqueue (producer — lock-free via CAS)
 *
 * 1. CAS-claim a slot by advancing head
 * 2. Write data to the slot
 * 3. Mark slot as ready (state: 0→1→2)
 */
kv_status_t ring_buffer_enqueue(ring_buffer_t *rb, const kv_command_t *cmd) {
    if (!rb || !cmd) return KV_ERR_INVALID;

    unsigned int head, next;
    for (;;) {
        head = atomic_load(&rb->head);
        unsigned int tail = atomic_load(&rb->tail);

        /* Check if full */
        next = (head + 1) & rb->mask;
        if (next == (tail & rb->mask) &&
            (head - tail) >= rb->capacity - 1) {
            return KV_ERR_FULL;
        }

        /* Try to claim this slot */
        if (atomic_compare_exchange_weak(&rb->head, &head, head + 1)) {
            break;
        }
        /* CAS failed — another producer got it, retry */
    }

    uint32_t slot = head & rb->mask;

    /* Write data */
    memcpy(&rb->buffer[slot], cmd, sizeof(kv_command_t));

    /* Mark as ready for consumer */
    atomic_store(&rb->slot_state[slot], 2); /* 2 = ready */
    return KV_OK;
}

/*
 * Dequeue (consumer — single consumer, no CAS needed)
 */
kv_status_t ring_buffer_dequeue(ring_buffer_t *rb, kv_command_t *out_cmd) {
    if (!rb || !out_cmd) return KV_ERR_INVALID;

    unsigned int tail = atomic_load(&rb->tail);
    unsigned int head = atomic_load(&rb->head);

    if (tail >= head) {
        return KV_ERR_EMPTY;
    }

    uint32_t slot = tail & rb->mask;

    /* Wait for producer to finish writing (spin until state == 2) */
    int spins = 0;
    while (atomic_load(&rb->slot_state[slot]) != 2) {
        if (++spins > 1000000) {
            return KV_ERR_EMPTY; /* safety: don't spin forever */
        }
        /* Yield CPU */
        SwitchToThread();
    }

    /* Read data */
    memcpy(out_cmd, &rb->buffer[slot], sizeof(kv_command_t));

    /* Mark slot as empty */
    atomic_store(&rb->slot_state[slot], 0);

    /* Advance tail */
    atomic_fetch_add(&rb->tail, 1);
    return KV_OK;
}

uint32_t ring_buffer_size(ring_buffer_t *rb) {
    if (!rb) return 0;
    unsigned int h = atomic_load(&rb->head);
    unsigned int t = atomic_load(&rb->tail);
    return h >= t ? h - t : 0;
}

bool ring_buffer_is_empty(ring_buffer_t *rb) {
    return ring_buffer_size(rb) == 0;
}
