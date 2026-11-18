#pragma once



#include <string>
#include <cstdint>
#include <unordered_map>
#include <map>
#include <functional>
#include <vector>
#include <optional>
#include <atomic>
#include <memory>
#include <new>
#include <utility>
#include "slab_allocator.h"
#include "absl/container/flat_hash_map.h"
#include <x86intrin.h> // MSVC : <intrin.h>






//NOTE: CPU CAN NEVER REORDER A WRITE BEFORE A LOAD OF THE SAME VARIABLE; BUT DIFFERENT THREADS CAN EXECUTE IN ANY ORDER
template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert(Capacity && ((Capacity & Capacity-1)) == 0, "Capacity must be a power of 2");
private:
    // make sure head/tail caches are stored in appropriate cachelines to avoid invalidation from different cpu cores
    alignas(64) std::atomic<std::size_t> head{}; //use atomic to ensure that indices are updated fully before usage
    std::size_t tail_cache{};

    alignas(64) std::atomic<std::size_t> tail{}; //in other words reads writes happen according to as they are sequenced in the code
    std::size_t head_cache{};
    struct Slot {
        alignas(T) unsigned char raw[sizeof(T)];
        //reserves uninitialized memory
    };
    T* slot(std::size_t i) { return std::launder(reinterpret_cast<T*>(buffer[i & (Capacity - 1)].raw)); }
    // launder tells compiler to forget any odl assumptions about a memory address, and treat the poitner as pointing to a brean-dew object currently living there
    std::unique_ptr<Slot[]> buffer{std::make_unique<Slot[]>(Capacity)};



public:

    template <typename... Args>
    bool try_push(Args&&...args); // write past tail (tail >= head), increment tail
    bool try_pop(T& out); // consume before tail, after head, increment head
    bool empty() const;
    std::size_t size() const;
};

//Basic Single Producer Single Consumer Queue
template <typename T, std::size_t Capacity>
template <typename... Args>
//do not cache tail for producer and head for consumer, otherwise we woudl need to load it everytime (defeats the purpose)
bool SPSCQueue<T, Capacity>::try_push(Args&&...args) { // producer thread
    const auto t = tail.load(std::memory_order_relaxed); // doesn't enforce a global cpu timeline, but ensures that
    // simple hardware instructions don't break down; for instance, if something else were to update tail,
    // producer has to wait so that it's cache line is fully valid again
    if (t - head_cache == Capacity) { //checks if the buffer is full by seeing if the written items between head and tail is the entire buffer  load command
        //if this not true, than at minimum, we have Capacity - (tail-head_cache) open slots to push to
        head_cache = head.load(std::memory_order_acquire);
        //update head cache to see if consumer has read anything out
        //allows producer to use true head value to see if we can push to buffer
        if (t - head_cache == Capacity) {
            return false;
        }
    }
    ::new (slot(t)) T(std::forward<Args>(args)...);
    tail.store(t+1, std::memory_order_release);  // ensure that writes prior to tail increment occurs prior
    //so consumer won't consume before v is written
    return true;
}

template<typename T, std::size_t Capacity>
bool SPSCQueue<T, Capacity>::try_pop(T& out) {
    const auto h = head.load(std::memory_order_relaxed); // also note that we don't
    // really have a situation where a write occurs prior to this load

    if (h == tail_cache) { //if this is not true, than at minimum, we should have tail_cache-h items in the queue already
        tail_cache = tail.load(std::memory_order_acquire);
        //update tail if producer has added an element to the queue
        if (h == tail_cache) {
            return false;
        }
    }
    T* p = slot(h);
    out = std::move(*p);
    p->~T();
    //avoid expensive copy
    head.store(h+1,std::memory_order_release);
    return true;
}

template<typename T, std::size_t Capacity>
bool SPSCQueue<T, Capacity>::empty() const {
    std::size_t c_head = head.load(std::memory_order_relaxed);
    std::size_t c_tail = tail.load(std::memory_order_relaxed);
    return c_head == c_tail;
}

template <typename T, std::size_t Capacity>
std::size_t SPSCQueue<T, Capacity>::size() const {
    std::size_t c_head = head.load(std::memory_order_relaxed);
    std::size_t c_tail = tail.load(std::memory_order_relaxed);
    return c_tail-c_head;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Classic Multi Producer Multi Consumer Bounded Queue
// This queue is not currently supported but will be integrated into the HFT system later
template<typename T, std::size_t Capacity>
class MPMCQueue {
    static_assert(Capacity && ((Capacity & Capacity-1)) == 0, "Capacity must be a power of 2");
private:
    struct Cell {
        std::atomic<std::size_t> sequence; // guarantees indivisibility
        T data;
    };
    std::unique_ptr<Cell[]> buffer {new Cell[Capacity]}; // allocates a buffer on the heap
    alignas(64) std::atomic<std::size_t> enqueue{};
    alignas(64) std::atomic<std::size_t> dequeue{};
public:
    MPMCQueue() {
        for (std::size_t i; i < Capacity; ++i) {
            buffer[i].sequence.store(i, std::memory_order_relaxed);
        }
    }
    bool try_push(const T& v);
    bool try_pop(T& out);
};
template<typename T, std::size_t Capacity>
bool MPMCQueue<T, Capacity>::try_push(const T& v) {
    std::size_t position = enqueue.load(std::memory_order_relaxed); // load the position at the enqueue index
    while (true) {
        Cell& c = buffer[position & (Capacity - 1)]; //get the cell at the modulo wrapped enqueue index
        std::size_t sequence = c.sequence.load(std::memory_order_acquire);
        auto difference = static_cast<std::ptrdiff_t>(sequence) - static_cast<std::ptrdiff_t>(position);
        // cast to signed integer type
        // if diff < 0, this means seq < pos, and that means we're observing a stale entry
        // if diff == 0, this means seq = pos, and this means we can write to it
        // if diff > 0, this means seq > pos, meaning the thread is lagging behind and must wait until the enqueue index is updated
        if (difference == 0) {
            if (enqueue.compare_exchange_weak(position, position+1, std::memory_order_relaxed)) {
                // compares the enqueue index with the position index (what you stored at the start)
                // if still the same (no thread modified it), write the data and update the enqueue index
                c.data = v;
                c.sequence.store(position+1, std::memory_order_release);
                return true;
            }
        } else if (difference < 0) {
            return false;
        } else {
            position = enqueue.load(std::memory_order_relaxed);
        }

    }

}

template<typename T, std::size_t Capacity>
bool MPMCQueue<T, Capacity>::try_pop(T& out) {
    std::size_t position = dequeue.load(std::memory_order_relaxed);
    while (true) {
        Cell& c = buffer[position & (Capacity - 1)];
        std::size_t sequence = c.sequence.load(std::memory_order_acquire);
        auto difference=  static_cast<std::ptrdiff_t>(sequence) - static_cast<std::ptrdiff_t>(position + 1);
        if (difference == 0) {
            if (dequeue.compare_exchange_weak(position, position+1, std::memory_order_relaxed)) {
                // compares the dequeue index with the position index (what you stored at the start)
                // if still the same (no thread modified it), write the data and update the dequeue index
                out = std::move(c.data);
                c.sequence.store(position + Capacity, std::memory_order_release);
                return true;
            }
        } else if (difference < 0) {
            //queue is simply empty; producer needs to fill it
            return false;
        } else {
            //update position
            position = dequeue.load(std::memory_order_relaxed);
        }
    }

}




//NASDAQ ITCH MESSAGES are in Big Endian, i.e. 16 in one byte is 00001000 not 000010000
template <typename T>
T read_be(const uint8_t* ptr) {
    T val;
    std::memcpy(&val, ptr, sizeof(T));
    if constexpr (sizeof(T) == 2) return __builtin_bswap16(val);
    if constexpr (sizeof(T) == 4) return __builtin_bswap32(val);
    if constexpr (sizeof(T) == 8) return __builtin_bswap64(val);
    return val;
}


struct PriceLevel;

static_assert(alignof(uint8_t) == 1);
static_assert(alignof(uint16_t) == 2);
static_assert(alignof(uint32_t) == 4);
static_assert(alignof(uint64_t) == 8);




// //...base order struct...
// struct AddOrder {
//     uint64_t reference_number;
//     uint16_t stock_locate;
//     char side;
//     uint32_t shares;
//     // std::array<char, 8> stock;
//     uint32_t price;
//
//
//     PriceLevel* price_level;
//
//     AddOrder* prev = nullptr;
//     AddOrder* next = nullptr;
//
//
// };

// struct AddOrderWithMPID {
//     uint64_t reference_number;
//     uint16_t stock_locate;
//     char side;
//     uint32_t shares;
//     // std::array<char, 8> stock;
//     uint32_t price;
//     std::array<char, 4> market_participant_id;
// };

// 56 bytes, make sure to align to 64 bytes to prevent cache invalidation
struct alignas(64) Order {
    uint64_t t_ingress;
    uint64_t reference_number;
    uint64_t new_reference_number;
    PriceLevel* price_level = nullptr;
    Order* prev = nullptr;
    Order* next = nullptr;
    uint32_t shares;
    uint32_t price;
    uint16_t stock_locate;
    char type;
    char side;
};


//
// struct OrderExecuted {
//     uint64_t reference_number;
//     uint16_t stock_locate;
//     uint32_t executed_shares;
//     uint64_t match_number;
// };
// struct OrderExecutedWithPrice {
//     uint64_t reference_number;
//     uint16_t stock_locate;
//     uint32_t executed_shares;
//     uint64_t match_number;
//     char printable;
//     uint32_t execution_price;
// };
// struct OrderCancel {
//     uint64_t reference_number;
//     uint32_t cancelled_shares;
// };
// struct OrderDelete {
//     uint64_t reference_number;
// };
// struct OrderReplace {
//     uint64_t original_order_reference_number;
//     uint64_t new_order_reference_number;
//     uint16_t stock_locate;
//     uint32_t shares;
//     uint32_t price;
// };
struct PriceLevel {
    uint64_t total_shares{};
    uint32_t order_count{};

    Order* head = nullptr;
    Order* tail = nullptr;
};
//make sure this is trivially copyable
struct PriceLevelEntry {
    uint32_t price;
    PriceLevel* price_level;
};
struct SymbolBook {
    // std::map<uint32_t, PriceLevel, std::greater<>> bids;
    // std::map<uint32_t, PriceLevel> asks;
    // std::vector<PriceLevelEntry> bids;
    // std::vector<PriceLevelEntry> asks;
    std::vector<PriceLevelEntry> sides[2];

    Pool<PriceLevel> pl_pool;

    std::optional<uint32_t> get_best_bid() const;
    std::optional<uint32_t> get_best_ask() const;
    std::optional<uint32_t> get_spread() const;
    std::optional<uint32_t> get_midprice() const;
    uint64_t get_bid_depth() const;
    uint64_t get_ask_depth() const;
    std::optional<double> get_imbalance() const;
    std::vector<PriceLevelEntry>::iterator find(uint32_t price, bool is_bid);

    SymbolBook() :
    pl_pool{64, 64}

    {
        sides[1].reserve(64);
        sides[0].reserve(64);
    }
};

struct FastHash {
    using is_avalanching = void; // tells abseil oru has is well-mixed
    std::size_t operator()(uint64_t k) const noexcept {
        return (k * 11400714819323198485ULL) >> 32;
    }
};

class OrderBook {
private:
    Pool<Order> order_pool;
    absl::flat_hash_map<uint64_t, Order*> orders;
    std::array<SymbolBook, 65536> symbol_books;

public:

    OrderBook(std::size_t num_orders ) :
    order_pool{64, 64}
    // orders(64)
    {
    }



    void add_order(Order& add_order);
    void add_order_with_mpid(Order& add_order_with_mpid);
    void execute_order(const Order& execute_order);
    void execute_order_with_price(const Order& execute_order_with_price);
    void cancel_order(const Order& cancel_order);
    void delete_order(const Order& delete_order);
    void replace_order(Order& replace_order);


    // const AddOrder* find_order(uint64_t reference_number) const;
    std::size_t size() const;




};
Order decode_add_order(const std::span<const uint8_t>& message);
Order decode_add_order_with_MPID(const std::span<const uint8_t>& message);
Order decode_order_executed(const std::span<const uint8_t>& message);
Order decode_order_executed_with_price(const std::span<const uint8_t>& message);
Order decode_order_cancel(const std::span<const uint8_t>& message);
Order decode_order_delete(const std::span<const uint8_t>& message);
Order decode_order_replace(const std::span<const uint8_t>& message);



inline std::vector<PriceLevelEntry>::iterator SymbolBook::find(uint32_t price, bool is_bid) {
    auto& levels = sides[is_bid];
    auto it = levels.end();
    const auto begin = levels.begin();
    if (is_bid) {
        while (it != begin && (it-1)->price >= price) {
            --it;
        }
        // return std::lower_bound(bids.begin(), bids.end(), price, [] (const PriceLevel* const& pricelevel, uint32_t price) { return pricelevel->price < price; });
    } else {
        while (it != begin && (it-1)->price <= price) {
            --it;
        }
        // return std::lower_bound(asks.begin(), asks.end(), price, [] (const PriceLevel* const& pricelevel, uint32_t price) { return pricelevel->price > price;; });
    }
    return it;
}


// AddOrder decode_add_order(const std::vector<uint8_t>& message);
// AddOrderWithMPID decode_add_order_with_MPID(const std::vector<uint8_t>& message);
// OrderExecuted decode_order_executed(const std::vector<uint8_t>& message);
// OrderExecutedWithPrice decode_order_executed_with_price(const std::vector<uint8_t>& message);
// OrderCancel decode_order_cancel(const std::vector<uint8_t>& message);
// OrderDelete decode_order_delete(const std::vector<uint8_t>& message);
// OrderReplace decode_order_replace(const std::vector<uint8_t>& message);



// void print_symbolbook(const SymbolBook& symbolbook, std::string stock_ticker);



struct Statistics {
    uint64_t total_messages{};
    uint64_t add_messages{};
    uint64_t execution_messages{};
    uint64_t cancellations{};
    uint64_t deletions{};
    uint64_t replacements{};
    uint64_t enqueue{};
    uint64_t dequeue{};
};







