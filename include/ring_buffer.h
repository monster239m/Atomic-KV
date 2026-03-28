#ifndef REDIS_CLONE_RING_BUFFER_H
#define REDIS_CLONE_RING_BUFFER_H

#include "common.h"

/* ============================================================
 *  Lock-Free Ring Buffer (MPSC — Multiple Producer, Single Consumer)
 *
 *  Fixed-size circular buffer using atomic head/tail indices.
 *  Producers use CAS to claim a slot, consumer uses atomic load.
 *  Size MUST be a power of 2 for fast modulo via bitmask.
 * ============================================================ */

typedef struct {
    kv_command_t    *buffer;         /* array of command slots */
    atomic_int      *slot_state;     /* 0=empty, 1=writing, 2=ready */
    uint32_t         capacity;       /* must be power of 2 */
    uint32_t         mask;           /* capacity - 1 */
    CACHE_ALIGNED atomic_uint head;  /* next write position (producers) */
    CACHE_ALIGNED atomic_uint tail;  /* next read position (consumer)  */
} ring_buffer_t;

/* ---- API ---- */

/* Create ring buffer with given capacity (rounded up to power of 2). */
ring_buffer_t *ring_buffer_create(uint32_t capacity);

/* Destroy ring buffer. NOT thread-safe. */
void ring_buffer_destroy(ring_buffer_t *rb);

/* Enqueue a command. Thread-safe (lock-free, multiple producers).
 * Returns KV_OK on success, KV_ERR_FULL if buffer is full. */
kv_status_t ring_buffer_enqueue(ring_buffer_t *rb, const kv_command_t *cmd);

/* Dequeue a command. Single consumer only.
 * Returns KV_OK on success, KV_ERR_EMPTY if buffer is empty. */
kv_status_t ring_buffer_dequeue(ring_buffer_t *rb, kv_command_t *out_cmd);

/* Get current number of items in buffer (approximate). */
uint32_t ring_buffer_size(ring_buffer_t *rb);

/* Check if buffer is empty. */
bool ring_buffer_is_empty(ring_buffer_t *rb);

#endif /* REDIS_CLONE_RING_BUFFER_H */
