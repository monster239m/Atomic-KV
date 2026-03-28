# Atomic-KV

A high-performance, multithreaded Redis-inspired in-memory key-value store implementation in C. Features advanced data structures including skip lists, hash maps, radix trees, and ring buffers for optimal performance and scalability.

## Overview

Atomic-KV is a production-oriented study of distributed key-value store architecture. It implements a TCP-based server with comprehensive support for concurrent client connections, multiple data structure backends, and command-based operations similar to Redis.

**Key Features:**
- **Multithreaded Architecture:** Thread-safe operations using synchronization primitives
- **Advanced Data Structures:**
  - Skip Lists: O(log n) balanced search with simpler implementation than AVL trees
  - Hash Maps: Average O(1) lookup with separate chaining using skip lists
  - Radix Trees: Efficient prefix-based searches and range queries
  - Ring Buffers: Circular queue structures for efficient buffering
- **Network Server:** TCP-based server supporting multiple concurrent clients via Winsock2
- **Command Parser:** Extensible command processing system
- **CLI Client:** Interactive command-line client for server interaction

## Project Structure

```
Atomic-KV/
├── src/               # Core implementation
│   ├── main.c         # Server entry point
│   ├── server.c       # TCP server implementation
│   ├── command.c      # Command parsing and execution
│   ├── skiplist.c     # Skip list data structure
│   ├── hashmap.c      # Hash map implementation
│   ├── radix_tree.c   # Radix tree implementation
│   └── ring_buffer.c  # Ring buffer implementation
├── include/           # Header files
│   ├── server.h
│   ├── command.h
│   ├── common.h
│   ├── skiplist.h
│   ├── hashmap.h
│   ├── radix_tree.h
│   └── ring_buffer.h
├── client/            # CLI client implementation
│   └── cli.c
├── tests/             # Comprehensive test suite
│   ├── test_skiplist.c
│   ├── test_hashmap.c
│   ├── test_radix.c
│   ├── test_ring_buffer.c
│   └── test_stress.c
├── Makefile           # Build configuration
└── PROJECT_REPORT.md  # Detailed academic documentation
```

## Building

### Requirements
- GCC compiler (C11 standard or later)
- Windows system with Winsock2 support (MSVC or MinGW)
- GNU Make

### Build Instructions

```bash
# Build server and client
make all

# Build server only
make server

# Build client only
make client

# Run tests
make test_skiplist
make test_hashmap
make test_radix
make test_ring_buffer
make test_stress

# Clean build artifacts
make clean
```

## Running

### Start the Server
```bash
./redis-clone.exe
```

The server will start listening on `localhost:6379` (default Redis port).

### Connect with CLI Client
```bash
./client.exe
```

Then enter commands interactively:
```
> SET mykey myvalue
OK
> GET mykey
myvalue
> DEL mykey
(integer) 1
```

## Implementation Details

### Skip Lists
Skip lists provide a probabilistic alternative to balanced binary search trees. They offer O(log n) average-case complexity for search, insertion, and deletion with a simpler implementation than AVL or Red-Black trees.

**File:** `src/skiplist.c` | **Tests:** `tests/test_skiplist.c`

### Hash Maps
Hash maps use separate chaining with skip lists as the chain structure, combining the average O(1) lookup of hash tables with the ordered iteration capabilities of skip lists.

**File:** `src/hashmap.c` | **Tests:** `tests/test_hashmap.c`

### Radix Trees
Radix trees (compressed tries) optimize space usage for string keys while maintaining efficient prefix-based operations and range queries. Ideal for certain query patterns and auto-complete features.

**File:** `src/radix_tree.c` | **Tests:** `tests/test_radix.c`

### Ring Buffers
Circular buffer structures designed for efficient queue operations in bounded memory. Useful for high-throughput scenarios and producer-consumer patterns.

**File:** `src/ring_buffer.c` | **Tests:** `tests/test_ring_buffer.c`

## Performance Testing

Run the stress test to evaluate performance under concurrent load:

```bash
make test_stress
```

This test creates multiple threads performing simultaneous operations and reports:
- Total operations executed
- Operations per second
- Memory usage patterns
- Thread contention metrics

## Command Reference

The server supports standard Redis-like commands:

- `SET key value` - Store a key-value pair
- `GET key` - Retrieve value by key
- `DEL key [key ...]` - Delete one or more keys
- `EXISTS key [key ...]` - Check key existence
- `KEYS pattern` - Find keys matching pattern
- `FLUSHALL` - Clear all data
- `DBSIZE` - Get total number of keys

## Architecture Notes

### Thread Safety
The implementation uses mutex-based synchronization for critical sections. Each operation acquires necessary locks to prevent race conditions and maintain data consistency across concurrent accesses.

### Memory Management
Manual memory allocation and deallocation is carefully managed to prevent leaks. Each data structure includes proper cleanup routines for resource deallocation.

### Concurrency Model
The server uses a thread-per-connection model for simplicity. Incoming connections are handled by dedicated worker threads that parse commands and execute operations.

## Testing Strategy

The project includes comprehensive test coverage:
- **Unit Tests:** Individual data structure validation
- **Integration Tests:** Complete server workflows
- **Stress Tests:** High-concurrency scenarios with many simultaneous operations

Run all tests:
```bash
make test_skiplist && make test_hashmap && make test_radix && make test_ring_buffer && make test_stress
```

## Educational Value

This project demonstrates:
- **Systems Programming:** Threading, synchronization, memory management
- **Data Structures:** Implementation and optimization of multiple specialized structures
- **Network Programming:** TCP socket programming, protocol design, client-server communication
- **C Language:** Modern C practices, efficient coding patterns, careful resource management
- **Software Engineering:** Modular design, comprehensive testing, performance profiling

## Future Enhancements

- Persistence layer with RDB/AOF
- Pub/Sub messaging system
- Lua scripting support
- Cluster mode implementation
- Memory optimization with compression
- Additional data structures (lists, sets, sorted sets)

## References and Resources

- Redis official documentation: https://redis.io
- Skip Lists: Pugh, W. (1990). "Skip lists: A probabilistic alternative to balanced trees"
- Hash Table Design: Cormen, Leiserson, Rivest, Stein - Introduction to Algorithms
- Concurrent Programming patterns: Various ACM publications

## License

This project is created for educational purposes as part of Data Structures and Algorithms laboratory coursework.

## Author

**Year:** 2026
**Course:** Data Structures and Algorithms Laboratory (DSA LAB)

---

For detailed academic documentation, see [PROJECT_REPORT.md](PROJECT_REPORT.md).
