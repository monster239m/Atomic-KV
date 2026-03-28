CC       = gcc
CFLAGS   = -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iinclude
LDFLAGS  = -lws2_32

SRC_DIR  = src
INC_DIR  = include
TEST_DIR = tests
CLI_DIR  = client
OBJ_DIR  = obj

# Source files
SRCS = $(SRC_DIR)/skiplist.c \
       $(SRC_DIR)/hashmap.c \
       $(SRC_DIR)/radix_tree.c \
       $(SRC_DIR)/ring_buffer.c \
       $(SRC_DIR)/command.c \
       $(SRC_DIR)/server.c

OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

# Targets
SERVER = redis-clone.exe
CLIENT = client.exe

.PHONY: all server client tests clean test_skiplist test_hashmap test_radix test_ring_buffer test_stress

all: server client

server: $(OBJ_DIR) $(OBJS) $(OBJ_DIR)/main.o
	$(CC) $(CFLAGS) -o $(SERVER) $(OBJS) $(OBJ_DIR)/main.o $(LDFLAGS)
	@echo ">>> Built $(SERVER)"

client: $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLI_DIR)/cli.c $(LDFLAGS)
	@echo ">>> Built $(CLIENT)"

# Object compilation
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	if not exist $(OBJ_DIR) mkdir $(OBJ_DIR)

# Tests
test_skiplist: $(OBJ_DIR) $(OBJ_DIR)/skiplist.o
	$(CC) $(CFLAGS) -o tests\test_skiplist.exe $(TEST_DIR)/test_skiplist.c $(OBJ_DIR)/skiplist.o $(LDFLAGS)
	@echo ">>> Running skip list tests..."
	tests\test_skiplist.exe

test_hashmap: $(OBJ_DIR) $(OBJ_DIR)/skiplist.o $(OBJ_DIR)/hashmap.o
	$(CC) $(CFLAGS) -o tests\test_hashmap.exe $(TEST_DIR)/test_hashmap.c $(OBJ_DIR)/skiplist.o $(OBJ_DIR)/hashmap.o $(LDFLAGS)
	@echo ">>> Running hash map tests..."
	tests\test_hashmap.exe

test_radix: $(OBJ_DIR) $(OBJ_DIR)/radix_tree.o
	$(CC) $(CFLAGS) -o tests\test_radix.exe $(TEST_DIR)/test_radix.c $(OBJ_DIR)/radix_tree.o $(LDFLAGS)
	@echo ">>> Running radix tree tests..."
	tests\test_radix.exe

test_ring_buffer: $(OBJ_DIR) $(OBJ_DIR)/ring_buffer.o
	$(CC) $(CFLAGS) -o tests\test_ring_buffer.exe $(TEST_DIR)/test_ring_buffer.c $(OBJ_DIR)/ring_buffer.o $(LDFLAGS)
	@echo ">>> Running ring buffer tests..."
	tests\test_ring_buffer.exe

test_stress: $(OBJ_DIR) $(OBJS)
	$(CC) $(CFLAGS) -o tests\test_stress.exe $(TEST_DIR)/test_stress.c $(OBJS) $(LDFLAGS)
	@echo ">>> Running stress tests..."
	tests\test_stress.exe

tests: test_skiplist test_hashmap test_radix test_ring_buffer test_stress

clean:
	if exist $(OBJ_DIR) rmdir /s /q $(OBJ_DIR)
	if exist $(SERVER) del $(SERVER)
	if exist $(CLIENT) del $(CLIENT)
	if exist tests\*.exe del tests\*.exe
	@echo ">>> Cleaned"
