#pragma once
#include <cstdint>
#include <iostream>
#include <memory>
#include <new>
#include <utility>
#include <vector>
#include <variant>
#include <cstring>
#include <limits>
#include <new>

template <typename T>
class Pool {

private:

    union Slot {
        Slot* next;
        T value;

        Slot() {} // required so std::make_unique<Slot[]>(size) can allocate an array of slots
        ~Slot() {} // required so std::unique_ptr<Slot[]> can free the array without calling ~T() on free slots
    };

    std::vector<std::unique_ptr<Slot[]>> slabs;


    std::size_t m_capacity;
    std::size_t m_chunk_size;
    std::size_t m_live_count; //tracks T*s used by application
    std::size_t m_peak_live_count{0};

    Slot* m_free_head;
public:
    Pool(std::size_t capacity, std::size_t size) :
    m_free_head{ nullptr },
    m_capacity{ 0 },
    m_live_count{ 0 },
    m_chunk_size{ size }
    {
        add_chunk( capacity );

        // std::memset(m_free_head, 0, m_capacity * 4096);

    }

    Pool(const Pool&) = delete; // delete copy constructor
    Pool& operator=(const Pool&) = delete; // delete copy assignment operator
    // if we copy memory we result in two pools pointing to the same block; dangerous


    template <typename... Args>
    T* allocate(Args&&... args);


    void deallocate(T* obj);
    void add_chunk(std::size_t size);

    std::size_t live_count() const;
    std::size_t capacity() const;
    std::size_t available() const;
    std::size_t numChunks() const;
    std::size_t peak_live_count() const;
};

template <typename T>
template <typename... Args>
T* Pool<T>::allocate(Args&&... args) {
    ++m_live_count;

    m_peak_live_count = std::max(
        m_peak_live_count,
        m_live_count
    );
    if (m_free_head == nullptr) {
        if (m_chunk_size > std::numeric_limits<std::size_t>::max() / 4) {
            throw std::bad_alloc{};
        }
        m_chunk_size = m_chunk_size * 4;

        add_chunk(m_chunk_size); //add chunk if no memory is allocated yet
    }
    Slot* slot = m_free_head; //get the Slot pointed to by head
    m_free_head = slot->next; //update free head
    ++m_live_count;

    return new (&slot->value) T(std::forward<Args>(args)...);
    //return pointer to newly allocated object
    // usage of new: first parenthetical contains build location, second contains args
    // Slot transforms to hold the data
}

template <typename T>
void Pool<T>::deallocate(T* obj) {
    if (obj == nullptr) {
        return;
    }
    obj->~T(); //destroy T (technically the object is still left behind as garbage in the Slot)
    Slot* slot = reinterpret_cast<Slot*>(obj); //find memory location in slab
    // Slot now points to the next free Slot
    slot->next = m_free_head;
    m_free_head = slot;
    --m_live_count;
}


template <typename T> // adds a chunk to memory pool and creates free list of slots
void Pool<T>::add_chunk(std::size_t size) {
    if (size == 0 || size > std::numeric_limits<std::size_t>::max() / sizeof(Slot)) {
        throw std::bad_array_new_length{};
    }

    auto slab = std::make_unique<Slot[]>(size);

    for (std::size_t i{ size }; i > 0; --i) {
        slab[i-1].next = m_free_head;
        m_free_head = &slab[i-1]; //points to first available block of memory
        // m_free_head -> slab[0] -> slab[1] -> slab[2] -> ... -> nullptr
    }
    slabs.push_back(std::move(slab));

    m_capacity += size;
    // std::cout << "finished allocating first slab of memory";
}
template <typename T>
std::size_t Pool<T>::live_count() const { return m_live_count; }

template <typename T>
std::size_t Pool<T>::capacity() const { return m_capacity; }

template <typename T>
std::size_t Pool<T>::available() const { return m_capacity - m_live_count; }
template <typename T>
std::size_t Pool<T>::numChunks() const { return slabs.size(); }

template <typename T>
std::size_t Pool<T>::peak_live_count() const {
    return m_peak_live_count;
}
