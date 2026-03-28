/*
 * command.c — Command Parser & Executor
 *
 * Parses text-based commands and executes them against the store.
 */

#include "command.h"

/* ============================================================
 *  Parse a raw input line into a command struct
 * ============================================================ */
kv_status_t command_parse(const char *input, kv_command_t *cmd) {
    if (!input || !cmd) return KV_ERR_INVALID;

    memset(cmd, 0, sizeof(kv_command_t));

    /* Skip leading whitespace */
    while (*input == ' ' || *input == '\t') input++;

    /* Remove trailing \r\n */
    char buf[KV_MAX_KEY_LEN + KV_MAX_VALUE_LEN + 64];
    strncpy(buf, input, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    int len = (int)strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' ')) {
        buf[--len] = '\0';
    }

    if (len == 0) return KV_ERR_INVALID;

    /* Extract command word */
    char cmd_word[32] = {0};
    int pos = 0;
    while (buf[pos] && buf[pos] != ' ' && pos < 31) {
        cmd_word[pos] = buf[pos];
        pos++;
    }
    cmd_word[pos] = '\0';

    /* Skip space after command */
    while (buf[pos] == ' ') pos++;

    /* Uppercase the command word for case-insensitive matching */
    for (int i = 0; cmd_word[i]; i++) {
        if (cmd_word[i] >= 'a' && cmd_word[i] <= 'z')
            cmd_word[i] -= 32;
    }

    /* Match command */
    if (strcmp(cmd_word, "SET") == 0) {
        cmd->type = CMD_SET;
        /* Parse key */
        int k = 0;
        while (buf[pos] && buf[pos] != ' ' && k < KV_MAX_KEY_LEN - 1) {
            cmd->key[k++] = buf[pos++];
        }
        cmd->key[k] = '\0';
        if (k == 0) return KV_ERR_INVALID;

        /* Skip space */
        while (buf[pos] == ' ') pos++;

        /* Rest is value */
        strncpy(cmd->value, buf + pos, KV_MAX_VALUE_LEN - 1);
        cmd->value[KV_MAX_VALUE_LEN - 1] = '\0';
        if (strlen(cmd->value) == 0) return KV_ERR_INVALID;

    } else if (strcmp(cmd_word, "GET") == 0) {
        cmd->type = CMD_GET;
        strncpy(cmd->key, buf + pos, KV_MAX_KEY_LEN - 1);
        cmd->key[KV_MAX_KEY_LEN - 1] = '\0';
        if (strlen(cmd->key) == 0) return KV_ERR_INVALID;

    } else if (strcmp(cmd_word, "DEL") == 0) {
        cmd->type = CMD_DEL;
        strncpy(cmd->key, buf + pos, KV_MAX_KEY_LEN - 1);
        cmd->key[KV_MAX_KEY_LEN - 1] = '\0';
        if (strlen(cmd->key) == 0) return KV_ERR_INVALID;

    } else if (strcmp(cmd_word, "KEYS") == 0) {
        cmd->type = CMD_KEYS;
        /* key holds the prefix pattern (may end with *) */
        strncpy(cmd->key, buf + pos, KV_MAX_KEY_LEN - 1);
        cmd->key[KV_MAX_KEY_LEN - 1] = '\0';
        /* Remove trailing * if present */
        int klen = (int)strlen(cmd->key);
        if (klen > 0 && cmd->key[klen - 1] == '*') {
            cmd->key[klen - 1] = '\0';
        }

    } else if (strcmp(cmd_word, "RANGE") == 0) {
        cmd->type = CMD_RANGE;
        /* Parse start key */
        int k = 0;
        while (buf[pos] && buf[pos] != ' ' && k < KV_MAX_KEY_LEN - 1) {
            cmd->key[k++] = buf[pos++];
        }
        cmd->key[k] = '\0';
        while (buf[pos] == ' ') pos++;
        /* Parse end key */
        strncpy(cmd->end_key, buf + pos, KV_MAX_KEY_LEN - 1);
        cmd->end_key[KV_MAX_KEY_LEN - 1] = '\0';

    } else if (strcmp(cmd_word, "PING") == 0) {
        cmd->type = CMD_PING;

    } else if (strcmp(cmd_word, "INFO") == 0) {
        cmd->type = CMD_INFO;

    } else if (strcmp(cmd_word, "QUIT") == 0 || strcmp(cmd_word, "EXIT") == 0) {
        cmd->type = CMD_QUIT;

    } else {
        cmd->type = CMD_UNKNOWN;
        strncpy(cmd->key, cmd_word, KV_MAX_KEY_LEN - 1);
        return KV_ERR_INVALID;
    }

    return KV_OK;
}

/* ============================================================
 *  Execute a parsed command
 * ============================================================ */
int command_execute(kv_command_t *cmd, hashmap_t *hm, radix_tree_t *rt,
                    char *response, int response_max) {
    if (!cmd || !hm || !response) return 0;

    char value_buf[KV_MAX_VALUE_LEN];
    kv_status_t status;

    switch (cmd->type) {
    case CMD_SET:
        status = hashmap_set(hm, cmd->key, cmd->value);
        if (status == KV_OK && rt) {
            radix_tree_insert(rt, cmd->key);
        }
        return snprintf(response, response_max,
                        status == KV_OK ? "+OK\r\n" : "-ERR %s\r\n",
                        kv_status_str(status));

    case CMD_GET:
        status = hashmap_get(hm, cmd->key, value_buf);
        if (status == KV_OK) {
            return snprintf(response, response_max, "$%s\r\n", value_buf);
        }
        return snprintf(response, response_max, "$-1\r\n"); /* nil */

    case CMD_DEL: {
        status = hashmap_del(hm, cmd->key);
        if (status == KV_OK && rt) {
            radix_tree_delete(rt, cmd->key);
        }
        return snprintf(response, response_max, ":%d\r\n",
                        status == KV_OK ? 1 : 0);
    }

    case CMD_KEYS: {
        if (!rt) {
            return snprintf(response, response_max, "*0\r\n");
        }
        char keys[1024][KV_MAX_KEY_LEN];
        int count = radix_tree_prefix_search(rt, cmd->key, keys, 1024);
        int written = snprintf(response, response_max, "*%d\r\n", count);
        for (int i = 0; i < count && written < response_max - KV_MAX_KEY_LEN - 10; i++) {
            written += snprintf(response + written, response_max - written,
                                "$%s\r\n", keys[i]);
        }
        return written;
    }

    case CMD_RANGE: {
        /* Use skip list range query on all buckets — simplified approach:
         * use radix tree prefix if available, else scan first bucket */
        char keys[1024][KV_MAX_KEY_LEN];
        int count = 0;

        /* Scan all buckets for range (brute force but correct) */
        for (int b = 0; b < KV_HASHMAP_BUCKETS && count < 1024; b++) {
            int found = skiplist_range(hm->buckets[b], cmd->key, cmd->end_key,
                                       keys + count, 1024 - count);
            count += found;
        }

        int written = snprintf(response, response_max, "*%d\r\n", count);
        for (int i = 0; i < count && written < response_max - KV_MAX_KEY_LEN - 10; i++) {
            written += snprintf(response + written, response_max - written,
                                "$%s\r\n", keys[i]);
        }
        return written;
    }

    case CMD_PING:
        return snprintf(response, response_max, "+PONG\r\n");

    case CMD_INFO: {
        long keys_count = hashmap_key_count(hm);
        long ops_count = hashmap_op_count(hm);
        int radix_count = rt ? radix_tree_count(rt) : 0;
        return snprintf(response, response_max,
                        "+INFO\r\n"
                        "keys:%ld\r\n"
                        "ops:%ld\r\n"
                        "radix_keys:%d\r\n"
                        "buckets:%d\r\n"
                        "workers:%d\r\n"
                        "\r\n",
                        keys_count, ops_count, radix_count,
                        KV_HASHMAP_BUCKETS, KV_WORKER_THREADS);
    }

    case CMD_QUIT:
        return snprintf(response, response_max, "+BYE\r\n");

    default:
        return snprintf(response, response_max,
                        "-ERR unknown command '%s'\r\n", cmd->key);
    }
}
