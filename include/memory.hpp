#pragma once

#include <stdlib.h>

#include <mutex>

#include "numa_helper.hpp"
#include "constants.hpp"

// Special memory allocation flag
#define MemoryIgnoreNuma -1
#define MemoryInterleaved -2
// Memory alignment is set to cache line size
#define MemoryDataAlignment CacheLineSize

/**
 * A counter that counting how many memory are needed.
 * In default, each memory segment is allocated in an aligned manner,
 * i.e. its size is a multiple of MemoryDataAlignment.
 */
class MemoryCounter {
    // Total size needed
    size_t data_size;
    // For debug
    bool aligned;

    // Get memory size. The size is rounded to be a multiple of MemoryDataAlignment.
    size_t get_aligned_size(size_t size) {
        if (size % MemoryDataAlignment != 0) {
            size += MemoryDataAlignment - size % MemoryDataAlignment;
        }
        return size;
    }

public:
    MemoryCounter() {
        data_size = 0;
        aligned = true;
    }

    size_t get_data_size() {
        return data_size;
    }

    bool is_aligned() {
        return aligned;
    }

    // With na_alloc, the size may not be a multipe of MemoryDataAlignment
    template<typename T>
    void na_alloc(size_t block_length = 1) {
        data_size += sizeof(T) * block_length;
        aligned = false;
    }

    // With al_alloc, the size must be a multipe of MemoryDataAlignment
    template<typename T>
    void al_alloc(size_t block_length = 1) {
        na_alloc<T>(block_length);
        align();
    }

    void align() {
        data_size = get_aligned_size(data_size);
        aligned = true;
    }
};

/**
 * Memory class is used to alloc memory in specified way (numa option):
 * - MemoryIgnoreNuma: Use NUMA-oblivious memory allocation.
 *   The memory address must be a multiple of MemoryDataAlignment.
 * - MemoryInterleaved: Allocate memory in a interleaved way.
 *   The memory address must be the begining of a page.
 * - A non-negative integer: Allocate memory to the specified NUMA node.
 *   The memory address must be the begining of a page.
 * The memory is either aligned or non-aligned:
 * - Non-aligned allocation: The memory size may not be a multipe of MemoryDataAlignment.
 * - Aligned allocation: The memory size is rounded to a multipe of MemoryDataAlignment.
 *
 * Memory class must be created by a memory pool as there is a mapping from logical NUMA option
 * to real NUMA option.
 */
class Memory {
    friend class MemoryPool;
    // The address of the allocated memory
    void* data;
    // The size of the allocated memory
    size_t data_size;
    // NUMA option
    int numa;
    // The counter serves like an iterator for return memory address
    // of multiple memory segments.
    MemoryCounter mcounter;

    Memory(MemoryCounter *pre_counter, int _numa = MemoryIgnoreNuma) {
        // Ensure alignement
        CHECK(pre_counter->is_aligned());
        data_size = pre_counter->get_data_size();
        numa = _numa;
        if (data_size != 0) {
            if (numa == MemoryIgnoreNuma) {
                data = aligned_alloc(MemoryDataAlignment, data_size);
            } else if (numa == MemoryInterleaved) {
                data = numa_alloc_interleaved(data_size);
            } else {
                data = numa_alloc_onnode(data_size, numa);
            }
            CHECK(data != NULL);
            memset(data, 0, data_size);
        } else {
            data = NULL;
        }
    }

public:
    ~Memory() {
        if (data != NULL) {
            // Ensure alignement
            CHECK(mcounter.get_data_size() == data_size);
            if (numa == MemoryIgnoreNuma) {
                free(data);
            } else {
                numa_free(data, data_size);
            }
        }
    }

    template<typename T>
    T* na_alloc(size_t block_length = 1) {
        void *p = static_cast<char*>(data) + mcounter.get_data_size();
        mcounter.na_alloc<T>(block_length);
        CHECK(mcounter.get_data_size() <= data_size);

        return static_cast<T*>(p);
    }

    template<typename T>
    T* na_alloc_new(size_t block_length = 1) {
        void* p = na_alloc<T>(block_length);
        if (block_length == 1) {
            return new(p)T();
        } else {
            return new(p)T[block_length];
        }

        return static_cast<T*>(p);
    }

    template<typename T>
    T* al_alloc(size_t block_length = 1) {
        T* ret = na_alloc<T>(block_length);
        align();
        return ret;
    }

    template<typename T>
    T* al_alloc_new(size_t block_length = 1) {
        T* ret = na_alloc_new<T>(block_length);
        align();
        return ret;
    }

    void align() {
        mcounter.align();
        CHECK(mcounter.get_data_size() <= data_size);
    }
};

/**
 * MemomryPool is used to manage memory allocation.
 * Once the MemoryPool is freed, all the memories in its pool
 * are also freed.
 */
class MemoryPool {
    std::vector<Memory*> pool;
    std::mutex lock;
    MultiThreadConfig mtcfg;

    int get_rectified_numa(int numa) {
        if (!mtcfg.with_numa()) {
            // If the configuration states that NUMA is not supported
            numa = MemoryIgnoreNuma;
        } else if (numa >= 0) {
            // Map the socket ID from logic ID to real physical ID
            numa = mtcfg.get_socket_mapping(numa);
        }
        return numa;
    }

public:
    MemoryPool(MultiThreadConfig _mtcfg) {
        mtcfg = _mtcfg;
    }

    ~MemoryPool() {
        clear();
    }

    void clear() {
        for (auto memory : pool) {
            delete memory;
        }
        pool.clear();
    }

    Memory* get_memory(MemoryCounter *mcounter, int numa = MemoryIgnoreNuma) {
        numa = get_rectified_numa(numa);

        Memory *memory = new Memory(mcounter, numa);
        std::lock_guard<std::mutex> guard(lock);
        pool.push_back(memory);
        return memory;
    }

    template<typename T>
    T* alloc(size_t block_length = 1, int numa = MemoryIgnoreNuma) {
        MemoryCounter mcounter;
        mcounter.al_alloc<T>(block_length);
        Memory* m = this->get_memory(&mcounter, numa);
        return m->al_alloc<T>(block_length);
    }

    template<typename T>
    T* alloc_new(size_t block_length = 1, int numa = MemoryIgnoreNuma) {
        void* p = static_cast<void*>(this->alloc<T>(block_length, numa));
        if (block_length == 1) {
            return new(p)T();
        } else {
            return new(p)T[block_length];
        }
    }
};
