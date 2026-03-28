#ifndef REDIS_CLONE_COMMAND_H
#define REDIS_CLONE_COMMAND_H

#include "common.h"
#include "hashmap.h"
#include "radix_tree.h"

/* ============================================================
 *  Command Parser & Executor
 *
 *  Parses text-based commands (simplified RESP-like):
 *    SET key value
 *    GET key
 *    DEL key
 *    KEYS prefix*
 *    RANGE start_key end_key
 *    PING
 *    INFO
 *    QUIT
 * ============================================================ */

/* Parse a raw input line into a command struct.
 * Returns KV_OK on success, KV_ERR_INVALID on parse failure. */
kv_status_t command_parse(const char *input, kv_command_t *cmd);

/* Execute a parsed command against the store.
 * Writes response into `response` buffer (must be >= KV_MAX_VALUE_LEN + 256).
 * Returns length of response. */
int command_execute(kv_command_t *cmd, hashmap_t *hm, radix_tree_t *rt,
                    char *response, int response_max);

#endif /* REDIS_CLONE_COMMAND_H */
