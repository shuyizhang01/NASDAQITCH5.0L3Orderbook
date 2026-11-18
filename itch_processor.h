#pragma once
#include <cstdint>
#include <vector>
#include <variant>
#include "orderbook.h"
#include <zlib.h>
#include <chrono>
#include <stdexcept>
#include <iostream>
#include <immintrin.h>
#include <numeric>
#include <thread>
#include <pthread.h>
#include <sched.h>
#include <cassert>
#include <cstring>
#include <x86intrin.h> // MSVC : <intrin.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <utility>

#include <thread>


inline bool pin_this_thread(const int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &set) == 0;
}


inline std::uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

template <typename Q> struct is_MPMC : std::false_type {};
//specialize for structs matching the MPMC Queue instantiation
template <typename T, std::size_t Capacity> struct is_MPMC<MPMCQueue<T, Capacity>> : std::true_type{};

template <typename Q> struct is_SPSC : std::false_type {};
//specialize for structs matching the SPSC Queue instantiation
template <typename T, std::size_t Capacity> struct is_SPSC<SPSCQueue<T, Capacity>> : std::true_type{};

template <typename Q>
concept IsMPMCQueue = is_MPMC<std::decay_t<Q>>::value;

template <typename Q>
concept IsSPSCQueue = is_SPSC<std::decay_t<Q>>::value;

// using OrderTypes = std::variant<
//     std::monostate,
//     AddOrder,
//     AddOrderWithMPID,
//     OrderExecuted,
//     OrderExecutedWithPrice,
//     OrderCancel,
//     OrderDelete,
//     OrderReplace>;
//
using Clock = std::chrono::steady_clock;

// struct alignas(64) QueuedOrder { //align to 64 bytes to prevent false sharing, so the L1 cache only holds Queued order
//     uint64_t t_ingress;
//     Order order;
//
// };
// static_assert(std::is_trivially_copyable_v<QueuedOrder>);
// static_assert(sizeof(QueuedOrder) <= 64);

struct alignas(64) LatencyLog {
    std::unique_ptr<uint64_t[]> m_samples;
    std::atomic<std::size_t> m_count{};
    std::size_t m_capacity;
    explicit LatencyLog(std::size_t capacity) :
    m_samples(std::make_unique<uint64_t[]>(capacity)),
    m_capacity(capacity)
    {
    }
    void record(uint64_t cycles) {
        std::size_t i = m_count.load(std::memory_order_relaxed);
        if (i < m_capacity) {
            m_samples[i] = cycles;
            m_count.store(i + 1, std::memory_order_release);// in case another thread reads latencies
        }
    }

};


template <typename T>
class Pipeline {
private:
    // gzFile reader + 1 MB message buffer
    int m_fd{};
    std::vector<uint8_t> m_message_buffer;
    // std::vector<uint8_t> m_message;

    //stats

    uint64_t m_messages_to_process{};
    std::atomic<uint64_t> m_message_count{};





    // threads
    std::vector<int> m_cores;
    std::size_t m_n_prod, m_n_con;

    static constexpr std::size_t NUM_SHARDS = 1;
    static constexpr std::size_t SHARD_MASK = NUM_SHARDS-1;

    alignas(64) std::array<OrderBook, NUM_SHARDS> m_order_books;
    std::array<T, NUM_SHARDS> m_order_queues{};

    std::vector<std::thread> m_threads;


    //flag
    std::atomic<bool> m_running{ false };


    // MPMC Queue only
    std::atomic<std::size_t> m_producers_left;
    std::vector<std::unique_ptr<LatencyLog>> m_queue_latencies;

    using Body = void (Pipeline::*)(int id);  //function poitner that poitners to member functino   ; takes no argumetns


    void spawn_thread(int cpu, std::string_view name, Body body, int id) {
        std::cout << "Spawning " << name << " thread " << id+1 << " with" << " cpu id: " << cpu << '\n';
        m_threads.emplace_back([=, this] {
            //pins thread first to core
                // ensures that the physical RAM is allocated on the same hardware
                // //chip/socket where your pinned thread is running
                // //            avoids sending requests across interconnect bus
            if (cpu >= 0 && !pin_this_thread(cpu)) {                   //a logical cpu of ID of -1 means we wish not to pin it,
                std::cout << std::format("pin failed: {} cpu={}\n", name, cpu, id);
            }
            (this->*body)(id);              //run member function
        });
    }



    template <std::size_t... Is> //Helper function exands 0,1,...,N-1 at compile time
    static std::array<OrderBook, NUM_SHARDS> make_order_books(
            std::size_t msgs, std::index_sequence<Is...>) {
            return {{ ((void)Is, OrderBook(msgs))... }};
            }



public:
    //order cores sothat adjacent entries share a cache domain (i.e. consecutive evens or odds)
    explicit Pipeline(int fd, std::size_t n_prod, std::size_t n_con, std::size_t messages_to_process, std::vector<int> cores) :
    m_fd{ fd },
    m_message_buffer(1 << 26),
    // m_message(40), //40 is max size of message
    m_messages_to_process{ messages_to_process },
    m_order_books(make_order_books(m_messages_to_process, std::make_index_sequence<NUM_SHARDS>{})),
    m_cores{ std::move(cores) },
    m_n_prod{ n_prod },
    m_n_con{ n_con },
    m_running{ true }

    {
        assert(NUM_SHARDS == m_n_con && "Invalid: number of threads must be equal to number of consumers"),
        m_queue_latencies.reserve(m_n_con); // do this upfront to avoid SGV and data races
    }


    //make sure pipeline is never moved/copied to avoid danger
    //memory reallocations
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    Pipeline(const Pipeline&&) = delete;
    Pipeline& operator=(const Pipeline&&) = delete;



    void feed_handler_single_thread(int id);
    void order_book_processor_single_thread(int id);
    void feed_handler_multi_thread(int id);
    void order_book_processor_multi_thread(int id);
    void producer_helper(const char* cur_char, const char* end_char, int id);
    bool process_helper(Order& out, int id);
    void start();
    void join();
    std::vector<std::unique_ptr<LatencyLog>>& get_queue_latencies();
};




template<typename T>
void Pipeline<T>::start() {
    if constexpr (IsSPSCQueue<T>) {
        std::cout << "Spinning threads for SPSC Queues..." << '\n';
        assert(m_n_prod == 1);
        for (std::size_t i{}; i < m_n_con; ++i) {
            m_queue_latencies.push_back(std::make_unique<LatencyLog>(m_messages_to_process)); //create a LatencyLog allocated on the heap with capacity of 1 million
        }// ensure this happens before we spawn the threads to avoid data races
        spawn_thread(m_cores.at(0), "producer", &Pipeline::feed_handler_single_thread, 0);
        for (int i{}; i < m_n_con; ++i) {
            spawn_thread(m_cores.at(i+1), "consumer", &Pipeline::order_book_processor_single_thread, i);
        }
        std::cout << "Queue Latencies Created" << '\n';



    } else if constexpr (IsMPMCQueue<T>) {

        std::cout << "Spinning threads for MPMC Queue..." << '\n';
        const bool pin = m_n_prod + m_n_con <= m_cores.size();
        // if more producer/consumer tasks than threads, elect not to pin to avoid OS scheduling overhead
        if (!pin) {
            std::cout << "threads > cores: running unpinned";
        }
        std::size_t k{};
        for (std::size_t i{}; i < m_n_con; ++i) {
            m_queue_latencies.push_back(std::make_unique<LatencyLog>(m_messages_to_process)); //create a LatencyLog allocated on the heap with capacity of 1 million
        }

        for (std::size_t i{}; i  < m_n_prod; ++i) {
            spawn_thread(pin ? m_cores[k++] : -1, "producer", &Pipeline::feed_handler_multi_thread, i);
        }
        for (std::size_t i{}; i < m_n_con; ++i) {
            spawn_thread(pin ? m_cores[k++] : -1, "consumer", &Pipeline::order_book_processor_multi_thread, i);
        }
        // std::cout << "Queue Latencies Created" << '\n';





    }
}

template <typename T>
void Pipeline<T>::join() {
    for (auto& t: m_threads) {
        if (t.joinable()) {
            t.join();
        }
    }
}

//SPSC PC Functions
template <typename T>
void Pipeline<T>::feed_handler_single_thread(const int id) {
    std::size_t leftover_bytes{};
    std::cout << "Starting to read in messages..." << '\n';
    struct stat st{};
    if (fstat(m_fd, &st) < 0) {
        std::cerr << "fstat failed\n";
        m_running.store(false, std::memory_order_release);
    }

    const std::size_t f_s = static_cast<std::size_t>(st.st_size);
    const char* data = static_cast<const char*>(
        mmap(nullptr, f_s, PROT_READ, MAP_PRIVATE, m_fd, 0)
    );
    if (data == MAP_FAILED) {
        std::cerr << "mmap failed\n";
        m_running.store(false, std::memory_order_release);
        return;


    }
    //tell linux kernel to stream pages ahead sequentially and drop pages behind
    madvise(const_cast<char*>(data), f_s, MADV_SEQUENTIAL);

    std::cout << "Starting to read in messages..." << '\n';
    producer_helper(data, data + f_s, id);


    // while (m_running.load(std::memory_order_relaxed) && m_message_count.load(std::memory_order_relaxed) < m_messages_to_process) {
    //     uint8_t* write_ptr = m_message_buffer.data() + leftover_bytes;
    //     std::size_t space_ava = m_message_buffer.size() - leftover_bytes;
    //
    //     int bytes_read = read(m_fd, write_ptr, space_ava);
    //     // std::cout << bytes_read << "bytes read" << '\n';
    //     uint64_t current_count = m_message_count.load(std::memory_order_relaxed);
    //     // std::cout << current_count << '\n';
    //     leftover_bytes = producer_helper(bytes_read);
    // }
    m_running.store(false, std::memory_order_release);
}

template <typename T>
void Pipeline<T>::order_book_processor_single_thread(const int id) {
    Order out{};
    while (m_running.load(std::memory_order_acquire)) {
        if (!process_helper(out, id)) {
            _mm_pause();
        }
    }
    process_helper(out, id);

}

//MPMC PC Functions
template<typename T>
void Pipeline<T>::feed_handler_multi_thread(const int id) {
    std::cout << "Processing messages on thread: " << id << '\n';
    uint64_t total_messages{};
    while (m_running.load(std::memory_order_relaxed) && total_messages < m_messages_to_process) {
        std::cout << "thread: " << id << " has processed " << total_messages << " messages" << '\n';
        producer_helper();
        ++total_messages;
    }
    m_producers_left.fetch_sub(1, std::memory_order_acq_rel); //perform atomic readmodifywrite
}

template<typename T>
void Pipeline<T>::order_book_processor_multi_thread(const int id) {
    Order out{};
    while (true) {
        const bool done = m_producers_left.load(std::memory_order_acquire) == 0;
        const bool got = process_helper(out, id); //if we processed, the queue had items, so no need to pause
        if (done) {
            break;
        }
        if (!got) _mm_pause(); //pause to avoid shared cache liens
    }
}

//Helpers used for both SPSC and MPMC

template <typename T>
void Pipeline<T>::producer_helper(const char* cur_char, const char* end_char, int id) {
    const auto* cur = reinterpret_cast<const uint8_t*>(cur_char);
    const auto* end = reinterpret_cast<const uint8_t*>(end_char);

    while (cur + 2 <= end && m_running.load(std::memory_order_relaxed) && m_message_count.load(std::memory_order_relaxed) < m_messages_to_process * m_n_con) {

        // __builtin_prefetch(m_message_buffer.data() + offset + 2, 0, 0);
        //prefetch cache lines before decoding messages

        uint16_t raw_len;
        std::memcpy(&raw_len, cur, sizeof(uint16_t));
        uint16_t msg_len = __builtin_bswap16(raw_len);
        cur += 2;
        //NASDAQ is in Big Endian format
        //we need to swap MSB and LSB (B = Byte)
        if (cur + msg_len > end) {
            break;
        }
        // if (msg_len == 0 || msg_len > 4096) {
        //     throw std::runtime_error("Invalid ITCH message length");
        // }

        auto m_message = std::span<const uint8_t>(cur, msg_len); // C++20 or greater
        Order order{};
        uint8_t type = m_message[0];
        if (type == 'A')  [[likely]] {
            order =  decode_add_order(m_message);
            order.t_ingress = __rdtsc();
            //measure time of order creation to order dequeue
            const std::size_t shard = order.stock_locate & SHARD_MASK;
            auto& queue = m_order_queues[shard];
            while (!(queue.try_push(order))) {
                _mm_pause();
                //queued_order.t_ingress = __rdtsc();
            };
            ++m_message_count;
        } else if (type == 'D') [[likely]] {
            order =  decode_order_delete(m_message);
            order.t_ingress = __rdtsc();
            const std::size_t shard = order.stock_locate & SHARD_MASK;
            auto& queue = m_order_queues[shard];
            while (!(queue.try_push(order))) {
                _mm_pause();
                //queued_order.t_ingress = __rdtsc();
            }
            ++m_message_count;
        } else if (type == 'E') [[likely]] {
            order =  decode_order_executed(m_message);
            order.t_ingress = __rdtsc();
            const std::size_t shard = order.stock_locate & SHARD_MASK;
            auto& queue = m_order_queues[shard];
            while (!(queue.try_push(order))) {
                _mm_pause();
                //queued_order.t_ingress = __rdtsc();
            }
            ++m_message_count;
        } else if (type == 'X') [[likely]] {
            order =  decode_order_cancel(m_message);
            order.t_ingress = __rdtsc();
            const std::size_t shard = order.stock_locate & SHARD_MASK;
            auto& queue = m_order_queues[shard];
            while (!(queue.try_push(order))) {
                _mm_pause();
                //queued_order.t_ingress = __rdtsc();
            }
            ++m_message_count;
        } else if (type == 'U') [[likely]] {
            order =  decode_order_replace(m_message);
            order.t_ingress = __rdtsc();
            const std::size_t shard = order.stock_locate & SHARD_MASK;
            auto& queue = m_order_queues[shard];
            while (!(queue.try_push(order))) {
                _mm_pause();
                // queued_order.t_ingress = __rdtsc();
            }
            ++m_message_count;
        } else {
            switch (type) {
                case 'F': {
                    order =  decode_add_order_with_MPID(m_message);
                    order.t_ingress = __rdtsc();
                    const std::size_t shard = order.stock_locate & SHARD_MASK;
                    auto& queue = m_order_queues[shard];
                    while (!(queue.try_push(order))) {
                        _mm_pause();
                        // queued_order.t_ingress = __rdtsc();
                    }
                    ++m_message_count;
                    break;
                }
                case 'C': {
                    order =  decode_order_executed_with_price(m_message);
                    order.t_ingress = __rdtsc();
                    const std::size_t shard = order.stock_locate & SHARD_MASK;
                    auto& queue = m_order_queues[shard];
                    while (!(queue.try_push(order))) {
                        _mm_pause();
                        // queued_order.t_ingress = __rdtsc();
                    }
                    ++m_message_count;
                    break;
                }
                default:
                    break;
            }
        }
    cur += msg_len;
    }
}

template <typename T>
bool Pipeline<T>::process_helper(Order& out, const int id) {
    bool got{};
    auto& order_book = m_order_books[id];
    auto& queue = m_order_queues[id];
    while (queue.try_pop(out)) {
        // std::cout << "queue size" << m_order_queue.head.load(std::memory_order_relaxed) - m_order_queue.tail.load(std::memory_order_relaxed) << '\n';
        // if ((m_message_count & (127)) == 0) {
        //     std::cout << "thread: " << id << "|" << " order" << " size: " << m_order_queues[id].size() << "| Queue size: " << m_order_queues[id].size() << '\n';
        // }
        const auto type = out.type;
        if (type == 'A') [[likely]] {
            order_book.add_order(out);
        } else if (type == 'D') [[likely]] {
            order_book.delete_order(out);
        } else if (type == 'E') [[likely]] {
            order_book.execute_order(out);
        } else if (type == 'X') [[likely]] {
            order_book.cancel_order(out);
        } else if (type == 'U') [[likely]] {
            order_book.replace_order(out);
        } else {
            if (type == 'F') {
                order_book.add_order(out);
            } else if (type == 'C') {
                order_book.execute_order(out);
            }
        }
        if ((m_message_count & 127) == 0) {
            m_queue_latencies[id]->record(__rdtsc() - out.t_ingress);
        }
        got = true;
    }
    return got;
}

template <typename T>
std::vector<std::unique_ptr<LatencyLog>>& Pipeline<T>::get_queue_latencies() {
    return m_queue_latencies;
}
