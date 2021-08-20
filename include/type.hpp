#pragma once

#include <stdint.h>

typedef uint32_t vertex_id_t;
typedef uint64_t edge_id_t;
typedef uint32_t walker_id_t;
typedef float real_t;
typedef uint16_t partition_id_t;
typedef uint32_t walker_state_t;

enum GraphFormat {
	BinaryGraphFormat,
	TextGraphFormat
};

enum TaskStatus {
    TWORKING,
    TCOMPLETE
};

/**
 * This configuration specifies:
 * - How many threads will be used and how many sockets will be used.
 *   Each socket will have the same number of threads.
 * - The mapping of logic socket ID to physical socket ID, e.g. we may
 *   want to use only the third NUMA node, then we can set the socket_num
 *   as 1 and the socket_mapping as 0->2.
 * - The L2 cache size.
 */
struct MultiThreadConfig {
private:
    std::vector<int> socket_mapping;
public:
    int thread_num;
    int socket_num;
    uint64_t l2_cache_size;

    bool with_numa() {
        // return socket_num > 1;
        return true;
    }
    int socket_id(int thread) {
        return thread / (thread_num / socket_num);
    }
    int socket_offset(int thread) {
        return thread % (thread_num / socket_num);
    }
    int socket_thread_num() {
        return thread_num / socket_num;
    }
    void set_default_socket_mapping() {
        socket_mapping.resize(socket_num);
        for (int s_i = 0; s_i < socket_num; s_i++) {
            socket_mapping[s_i] = s_i;
        }
    }
    void set_socket_mapping(std::vector<int> _map) {
        socket_mapping = _map;
    }
    int get_socket_mapping(int socket) {
        if (socket_mapping.size() == 0) {
            return socket;
        } else {
            return socket_mapping[socket];
        }
    }
};

enum SamplerClass {
    ClassExclusiveBufferSampler = 0,
    ClassDirectSampler = 1,
    ClassSamplerHintNum,
    ClassUniformDegreeDirectSampler,
    ClassSimilarDegreeDirectSampler,
    ClassBaseSampler,
};
