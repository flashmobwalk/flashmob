#pragma once

#include <functional>

#include "type.hpp"
#include "numa_helper.hpp"
#include "memory.hpp"
#include "log.hpp"
#include "timer.hpp"

struct Walker {
    walker_id_t id; // walker's id
    vertex_id_t vertex; // waker's current residing vertex
} __attribute__((packed));

/**
 * WalkerManager create walker-related array and do walker-related jobs
 * in a NUMA-aware manner.
 */
class WalkerManager {
    struct ThreadState {
        walker_id_t curr;
        walker_id_t end;
        TaskStatus status;
    };

    MultiThreadConfig mtcfg;
    MemoryPool mpool;
    walker_id_t max_epoch_walker_num;
    std::vector<std::vector<ThreadState*> > thread_states;
public:
    std::vector<std::vector<walker_id_t> > thread_walker_begin;
    std::vector<std::vector<walker_id_t> > thread_walker_end;
    std::vector<walker_id_t> socket_walker_begin;
    std::vector<walker_id_t> socket_walker_end;

    WalkerManager(MultiThreadConfig _mtcfg) : mpool(_mtcfg) {
        mtcfg = _mtcfg;
    }

    void init(walker_id_t _max_epoch_walker_num) {
        Timer timer;
        max_epoch_walker_num = _max_epoch_walker_num;

        walker_id_t page_walker_num = PageSize / std::min(sizeof(vertex_id_t), sizeof(partition_id_t));
        walker_id_t hint_socket_walker_num = max_epoch_walker_num / mtcfg.socket_num / page_walker_num * page_walker_num;

        walker_id_t s_remain_walker = max_epoch_walker_num;
        for (int s_i = 0; s_i < mtcfg.socket_num; s_i++) {
            walker_id_t socket_walker_num = s_i + 1 == mtcfg.socket_num ? s_remain_walker :  std::min(hint_socket_walker_num, s_remain_walker);
            s_remain_walker -= socket_walker_num;
            socket_walker_begin.push_back(s_i == 0 ? 0 : socket_walker_end[s_i - 1]);
            socket_walker_end.push_back(socket_walker_begin[s_i] + socket_walker_num);
        }

        int socket_thread_num = mtcfg.thread_num / mtcfg.socket_num;
        thread_walker_begin.resize(mtcfg.socket_num);
        thread_walker_end.resize(mtcfg.socket_num);
        for (int s_i = 0; s_i < mtcfg.socket_num; s_i++) {
            thread_walker_begin[s_i].resize(socket_thread_num, 0);
            thread_walker_end[s_i].resize(socket_thread_num, 0);
        }
        walker_id_t chunk_walker_num = MemoryDataAlignment / std::min(sizeof(vertex_id_t), sizeof(partition_id_t));
        walker_id_t hint_thread_walker_num = max_epoch_walker_num / mtcfg.thread_num / chunk_walker_num * chunk_walker_num;
        for (int s_i = 0; s_i < mtcfg.socket_num; s_i++) {
            walker_id_t t_remain_walker = socket_walker_end[s_i] - socket_walker_begin[s_i];
            for (int t_i = 0; t_i < socket_thread_num; t_i++) {
                walker_id_t thread_walker_num = t_i + 1 == socket_thread_num ? t_remain_walker : std::min(hint_thread_walker_num, t_remain_walker);
                t_remain_walker -= thread_walker_num;
                thread_walker_begin[s_i][t_i] = t_i == 0 ? socket_walker_begin[s_i] : thread_walker_end[s_i][t_i - 1];
                thread_walker_end[s_i][t_i] = thread_walker_begin[s_i][t_i] + thread_walker_num;
            }
        }

        thread_states.resize(mtcfg.socket_num);
        for (int s_i = 0; s_i < mtcfg.socket_num; s_i++) {
            thread_states[s_i].resize(socket_thread_num, nullptr);
        }
        for (int s_i = 0; s_i < mtcfg.socket_num; s_i++) {
            for (int t_i = 0; t_i < socket_thread_num; t_i++) {
                thread_states[s_i][t_i] = mpool.alloc_new<ThreadState>(1, s_i);
            }
        }
        LOG(WARNING) << block_mid_str() << "Initialize WalkerManager in " << timer.duration() << " seconds";
    }

    template<typename T>
    T * alloc_walker_array(size_t len = 1) {
        char * array = (char *)mmap(NULL, sizeof(T) * len * max_epoch_walker_num, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK(array!=NULL);
        for (int s_i = 0; s_i < mtcfg.socket_num; s_i++) {
            if (socket_walker_end[s_i] > socket_walker_begin[s_i]) {
                CHECK(sizeof(T) * len * socket_walker_begin[s_i] % PageSize == 0) << std::setbase(10) << ": socket_walker_begin[" << s_i << "] " << socket_walker_begin[s_i];
                numa_tonode_memory(array + sizeof(T) * len * socket_walker_begin[s_i], sizeof(T) * len * (socket_walker_end[s_i] - socket_walker_begin[s_i]), mtcfg.get_socket_mapping(s_i));
            }
        }
        // Initialize the array to ensure it's really allocated
        memset(array, 0, sizeof(T) * len * max_epoch_walker_num);
        return (T*)array;
    }

    template<typename T>
    void dealloc_walker_array(T* array, size_t len = 1) {
        CHECK(0 == munmap(array, max_epoch_walker_num * sizeof(T) * len));
    }

    void process_walkers(std::function<void(walker_id_t)> process, walker_id_t active_walker_num) {
        int socket_thread_num = mtcfg.thread_num / mtcfg.socket_num;
        const walker_id_t chunk_size = 64;
        #pragma omp parallel
        {
            int thread_id = omp_get_thread_num();
            int local_socket = mtcfg.socket_id(thread_id);
            int local_thread = mtcfg.socket_offset(thread_id);

            thread_states[local_socket][local_thread]->curr = thread_walker_begin[local_socket][local_thread];
            thread_states[local_socket][local_thread]->end = std::min(thread_walker_end[local_socket][local_thread], active_walker_num);
            thread_states[local_socket][local_thread]->status = TWORKING;
            #pragma omp barrier

            walker_id_t work = 0;
            for (int s_i = 0; s_i < mtcfg.socket_num; s_i++) {
                for (int t_i = 0; t_i < socket_thread_num; t_i++) {
                    int socket = (local_socket + s_i) % mtcfg.socket_num;
                    int thread = (local_thread + t_i) % socket_thread_num;
                    auto ts = thread_states[socket][thread];
                    auto &curr = ts->curr;
                    auto &end = ts->end;
                    auto &status = ts->status;
                    walker_id_t work_begin;
                    while (status == TWORKING && (work_begin = __sync_fetch_and_add(&curr, chunk_size)) < end) {
                        walker_id_t work_end = std::min(work_begin + chunk_size, end);
                        for (walker_id_t w_i = work_begin; w_i < work_end; w_i++) {
                            process(w_i);
                            work++;
                        }
                    }
                    if (s_i == 0 && t_i == 0) {
                        ts->status = TCOMPLETE;
                    }
                }
            }
        }
    }
};
