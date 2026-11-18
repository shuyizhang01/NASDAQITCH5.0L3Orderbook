#include <iostream>
#include <vector>
#include <cstdint>
#include <zlib.h>
#include <iomanip>
#include "orderbook.h"
#include "itch_processor.h"
#include <variant>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <thread>
#include <pthread.h>

// TIP To <b>Run</b> code, press <shortcut actionId="Run"/> or click the <icon src="AllIcons.Actions.Execute"/> icon in the gutter.


//LINUX/WSL based HFT system


int main() {
    //
    //The target system's Time Stamp Counter (TSC) frequency in Hertz
    constexpr double TSC_HZ = 2.611211e9;
    //int res =  pin_this_thread(6);
    //if (!res) {
    //    std::cerr << "Error pinning main thread: " << '\n';
    //}
    const char* path = "/home/paral/S061226-v50.full.bin";


    //cmake -S . -B cmake-build-perf   -DCMAKE_BUILD_TYPE=RelWithDebInfo   -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O3 -march=native -g -DNDEBUG"
    // -march=native tells the compiler to auto-detect the  exact architecture of the host CPU
    // assumes architecture is x86-64 (often called amd64)

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        std::cerr << "Failed to open file: " << path << '\n';
        return 1;
    }


    std::cout << "Successfully opened gzip file\n";
    std::vector cores{0, 2, 4, 6, 8, 10, 12, 14}; // reserve first core for producer
    std::size_t n_producers{ 1 };
    std::size_t n_consumers{ 1 }; // SPSCQueue can only have one producer and consumer.
    assert(!cores.empty() && n_consumers <= cores.size() - 1 && "Error: not enough cpu cores");
    uint64_t messages_to_process_per_thread{ 10000000 };
    auto pipeline = make_unique<Pipeline<SPSCQueue<Order, 1>>> ( fd, n_producers, n_consumers, messages_to_process_per_thread, cores );

    _mm_lfence();
    uint64_t start_tsc = __rdtsc();
    _mm_lfence();

    pipeline->start();
    pipeline->join();

    _mm_lfence();
    uint64_t end_tsc = __rdtsc();
    _mm_lfence();

    std::cout << "Finished processing." << '\n';
    auto logs = std::move(pipeline->get_queue_latencies());


    std::vector<uint64_t> merged;
    std::size_t total{};
    for (auto& log : logs) {
        total += log->m_count.load(std::memory_order_relaxed);
    }
    merged.reserve(total);
    for (auto& log : logs) {
        std::size_t n = log->m_count.load(std::memory_order_relaxed);
        merged.insert(merged.end(), log->m_samples.get(), log->m_samples.get() + n);
    }
    std::sort(merged.begin(), merged.end());
    std::cout << "Total size" << merged.size() << '\n';
    uint64_t p99 = merged[merged.size() * 99 / 100];
    uint64_t p50 = merged[merged.size() * 50 / 100];
    uint64_t p1 = merged[merged.size() * 1 / 100];
    std::cout << "P99 Latency: " << p99 / (TSC_HZ / 1e9) << "ns\n";
    std::cout << "P50 Latency: " << p50 / (TSC_HZ / 1e9) << "ns\n";
    std::cout << "P1 Latency: " << p1 / (TSC_HZ / 1e9) << "ns\n";
    std::cout << "Throughput: "
              << (static_cast<double>(messages_to_process_per_thread * n_consumers)
                  / (static_cast<double>(end_tsc - start_tsc) / TSC_HZ)) / 1e6
              << " million msgs/sec" << '\n';

    close(fd);
    return 0;
}