# Concurrent Key-Value Store in C++

A Redis-like in-memory key-value store built from scratch in C++17.

This project implements a TCP server that supports multiple concurrent clients, basic key-value commands, key expiry, LRU eviction, snapshot persistence, and benchmarking.

## Features

- TCP server using POSIX sockets
- Multiple concurrent clients handled by a worker thread pool
- Thread-safe shared store using mutex-protected critical sections
- In-memory key-value storage
- LRU eviction using a hash map + doubly linked list
- Key expiry with lazy deletion and background cleanup
- Snapshot persistence to disk
- Simple command protocol usable with `netcat`
- Benchmark client for throughput measurement

## Supported Commands

### SET

Stores a key-value pair.

```text
SET key value
```

Example:

```text
SET name alice
```

Response:

```text
+OK
```

### GET

Gets the value for a key.

```text
GET key
```

Example:

```text
GET name
```

Response:

```text
alice
```

If the key does not exist:

```text
(nil)
```

### DEL

Deletes a key.

```text
DEL key
```

Response:

```text
1
```

If the key does not exist:

```text
0
```

### EXPIRE

Sets a time-to-live for a key.

```text
EXPIRE key seconds
```

Example:

```text
EXPIRE name 10
```

Response:

```text
1
```

### TTL

Returns the remaining time-to-live for a key.

```text
TTL key
```

Responses:

```text
-2
```

Key does not exist.

```text
-1
```

Key exists but has no expiry.

```text
5
```

Key expires in 5 seconds.

## Build

```bash
g++ -std=c++17 -O2 -pthread main.cpp -o kvstore
```

## Run

```bash
./kvstore
```

The server listens on port `6380`.

## Manual Testing

In another terminal, connect using `nc`:

```bash
nc localhost 6380
```

Example session:

```text
SET name alice
+OK
GET name
alice
DEL name
1
GET name
(nil)
```

## LRU Eviction

The store has a fixed capacity. When the capacity is full, the least recently used key is evicted.

For example, with capacity 3:

```text
SET a 1
SET b 2
SET c 3
GET a
SET d 4
GET b
GET a
GET c
GET d
```

Expected result:

```text
+OK
+OK
+OK
1
+OK
(nil)
1
3
4
```

Explanation:

* `a`, `b`, and `c` are inserted.
* `GET a` makes `a` recently used.
* `SET d 4` exceeds capacity.
* `b` is evicted because it is now the least recently used key.

## Key Expiry

Keys can be given a TTL using `EXPIRE`.

Example:

```text
SET token abc123
EXPIRE token 3
GET token
```

Before expiry:

```text
abc123
```

After 3 seconds:

```text
GET token
(nil)
```

The store uses two expiry strategies:

* Lazy expiry: expired keys are removed when accessed.
* Active expiry: a background thread periodically removes expired keys.

## Persistence

The store periodically writes a snapshot of the current data to disk.

On startup, the server loads the snapshot file and restores saved keys.

This is a simple snapshot-based persistence system, similar in idea to Redis RDB persistence. Redis also supports AOF, or append-only file persistence, where each write command is logged. This project currently implements only snapshot persistence.

Tradeoff:

* Snapshotting is simple and fast.
* Recent writes may be lost if the server crashes before the next snapshot.

## Architecture

```text
Client connections
        |
        v
TCP listening socket
        |
        v
Accepted client sockets
        |
        v
Worker thread pool
        |
        v
Command handler
        |
        v
Mutex-protected store
        |
        v
Hash map + doubly linked list
        |
        +--> Expiry background thread
        |
        +--> Snapshot persistence thread
```

## Data Structures

The store uses two data structures together:

```text
unordered_map<string, Node*> lru_index
```

This gives O(1) lookup by key.

```text
Doubly linked list
```

This tracks usage order for LRU eviction.

* Most recently used keys are moved near the head.
* Least recently used keys stay near the tail.
* When capacity is full, the tail node is evicted.

This gives O(1) `GET`, `SET`, and eviction behavior.

## Concurrency Model

The server uses a fixed-size worker thread pool.

Accepted client sockets are pushed into a shared queue. Worker threads wait on a condition variable, pop client sockets from the queue, and handle client commands.

The key-value store itself is shared mutable state, so access is protected using a mutex.

Network I/O is kept outside the store lock where possible, so slow clients do not hold the mutex while writing responses.

## Benchmark

A Python benchmark client was used to open multiple connections and send many `SET` and `GET` commands.

Benchmark configuration:

```text
Total operations: 200000
Threads: 10
Build: g++ -std=c++17 -O2 -pthread main.cpp -o kvstore
```

Result:

```text
Elapsed time: 1.76 seconds
Throughput: 113737.42 ops/sec
```

## What I Learned

* How to build a TCP server using POSIX sockets
* How `socket`, `bind`, `listen`, `accept`, `recv`, and `write` work
* Why TCP is a byte stream and not a message-based protocol
* How to handle multiple clients using threads
* How race conditions happen with shared mutable state
* How mutexes and RAII locking protect critical sections
* How condition variables are used in a worker thread pool
* How key expiry works using timestamps and background cleanup
* How LRU eviction works using a hash map and doubly linked list
* How snapshot persistence serializes in-memory data to disk
* How to benchmark throughput using multiple client connections

## Limitations

* The command protocol is simple and line-based, not Redis RESP.
* TCP stream framing is simplified.
* A single global mutex protects the store, so reads and writes are serialized.
* Snapshot persistence may lose recent writes if the server crashes before the next snapshot.
* The snapshot format is simple and assumes keys and values do not contain tabs or newlines.
* The server does not yet support authentication, replication, or clustering.

## Future Improvements

* Implement proper stream buffering for partial and multiple commands per read
* Support the Redis RESP protocol
* Add append-only log persistence
* Use `std::shared_mutex` to allow concurrent reads
* Add more benchmark modes
* Add configurable capacity, port, and snapshot interval
* Split the code into multiple files
* Add unit tests for LRU, expiry, and persistence