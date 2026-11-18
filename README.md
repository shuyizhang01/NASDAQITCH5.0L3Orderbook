# Low-Latency ITCH Order Book

A high-performance C++23 market data processing engine for Level 3 (L3) order book reconstruction from Nasdaq TotalView-ITCH 5.0 binary feeds.

## Architecture

A multithreaded producer-consumer architecture with a single producer responsible for binary feed ingestion and message dispatch, supporting multiple consumer threads through SPSC queues.

The L3 order book maintains individual order-level state using Abseil's `flat_hash_map` for direct order-reference lookup, sorted bid/ask price-level vectors, and intrusive doubly linked lists for FIFO order priority within each price level. Custom slab allocators manage order and price-level memory to reduce dynamic allocation overhead.

Implements incremental Add, Execute, Cancel, Delete, and Replace operations, with price-level quantities maintained throughout order book updates. Stock Locate identifiers provide direct symbol indexing.

## Performance

Benchmarked using a decompressed binary Nasdaq ITCH feed (`S061226-v50.full.bin`) containing approximately 10 million messages from a single trading day, representing a portion of the day's total market activity.

Results averaged across 10 measured runs following 5 warm-up runs, using one producer thread, one consumer thread, and an SPSC queue capacity of 1.

| Metric | Average |
|---|---:|
| P99 latency | **917.93 ns** |
| P50 latency | **422.98 ns** |
| P1 latency | **277.38 ns** |
| Throughput | **3.48 million msgs/sec** |

Latency measurements include only valid order-book messages that generate measured processing events, not every raw ITCH feed message.

Benchmarks conducted under Linux (WSL2) with `SCHED_FIFO` real-time scheduling at priority 99.

## Build Configuration

Compiled with GCC 13.3.0 and C++23 using CMake 3.28.3. Release builds use `-O3`, `-march=native`, `-mtune=native`, and link-time optimization (`-flto=auto`), with `-fomit-frame-pointer` and `-fno-rtti`. RelWithDebInfo builds retain frame pointers for profiling with Linux `perf`.

Dependencies: zlib, Abseil (`flat_hash_map`), and POSIX threads.

## Benchmark Environment

Intel Core i7-1360P (13th Gen, 16 logical CPUs), x86_64, running Ubuntu 24.04 under WSL2.

## Technologies

C++23 · GCC · Linux · CMake · zlib · Abseil · SPSC Queues · Intrusive Doubly Linked Lists · Custom Slab Allocators
