#pragma once

#include <sys/syscall.h>

#include <numa.h>
#include <numaif.h>
#include <sys/mman.h>

#include <thread>
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <set>
#include <memory>
#include <algorithm>

#include <omp.h>

#include "log.hpp"
#include "type.hpp"
#include "compile_helper.hpp"
#include "sysinfo.hpp"

/**
 * Return at which NUMA node this memory is.
 */
int get_memory_socket_id(void* ptr) {
    int numa_node = -1;
    get_mempolicy(&numa_node, NULL, 0, (void*)ptr, MPOL_F_NODE | MPOL_F_ADDR);
    return numa_node;
}

/**
 * Assign the same amount of threads and bind them to specific sockets according to configuration.
 */
void init_concurrency(MultiThreadConfig mtcfg)
{
    LOG(WARNING) << block_begin_str() << "Configure multi-threading environment";
	int max_cores = get_max_core_num();
    int max_sockets = get_max_socket_num();

    if (mtcfg.with_numa()) {
        CHECK( numa_available() != -1 );
    }

    LOG(WARNING) << block_mid_str() << "Concurrency usage: thread " << mtcfg.thread_num << " of " << max_cores << ", socket " << mtcfg.socket_num << " of " << max_sockets;
    CHECK(mtcfg.socket_num <= max_sockets);

    if (mtcfg.with_numa()) {
        char nodestring[mtcfg.socket_num * 2];
        nodestring[0] = '0';
        for (int s_i=1;s_i<mtcfg.socket_num;s_i++) {
          nodestring[s_i*2-1] = ',';
          nodestring[s_i*2] = '0'+s_i;
        }
        nodestring[mtcfg.socket_num * 2 - 1] = 0;
        LOG(INFO) << block_mid_str() << "nodestr: " << nodestring;

        struct bitmask * nodemask = numa_parse_nodestring(nodestring);

        // set memory binding
        numa_set_interleave_mask(nodemask);
    }

    omp_set_dynamic(0);
    omp_set_num_threads(mtcfg.thread_num);

    if (mtcfg.with_numa()) {
        #pragma omp parallel for
        for (int t_i=0;t_i<mtcfg.thread_num;t_i++) {
          int s_i = mtcfg.get_socket_mapping(mtcfg.socket_id(t_i));
          // _unused(s_i);
          // this function is confict with GOMP_CPU_AFFINITY
          CHECK(numa_run_on_node(s_i)==0);
          // printf("thread-%d bound to socket-%d\n", t_i, s_i);
        }
    }

    std::vector<int> cpus(mtcfg.thread_num), nodes(mtcfg.thread_num);
    #pragma omp parallel
    {
        int worker_id = omp_get_thread_num();
        unsigned cpu, node;
        int status = syscall(SYS_getcpu, &cpu, &node, NULL);
        CHECK(status == 0);
        _unused(status);
        unsigned cpu2 = sched_getcpu();
        CHECK(cpu == cpu2);
        _unused(cpu2);
        cpus[worker_id] = cpu;
        nodes[worker_id] = node;
    }
    std::stringstream ss;
    ss << block_mid_str() << "Thread id:\t";
    for (int w_i = 0; w_i < mtcfg.thread_num; w_i++) {
        ss << "\t" << w_i;
    }
    LOG(WARNING) << ss.str();
    ss.str("");
    ss << block_mid_str() << "Thread cores:";
    for (int w_i = 0; w_i < mtcfg.thread_num; w_i++) {
        ss << "\t" << cpus[w_i];
    }
    LOG(WARNING) << ss.str();
    ss.str("");
    ss << block_mid_str() << "Thread nodes:";
    for (int w_i = 0; w_i < mtcfg.thread_num; w_i++) {
        ss << "\t" << nodes[w_i];
    }
    LOG(WARNING) << ss.str();

    #ifndef UNIT_TEST
    for (int w_i = 0; w_i < mtcfg.thread_num; w_i++) {
        CHECK(nodes[w_i] == mtcfg.get_socket_mapping(mtcfg.socket_id(w_i)));
    }
    std::sort(cpus.begin(), cpus.end());
    ss.str("");
    ss << block_mid_str() << "Thread cores (sorted):";
    for (int w_i = 0; w_i < mtcfg.thread_num; w_i++) {
        ss << "\t" << cpus[w_i];
    }
    LOG(WARNING) << ss.str();
    // CHECK(std::unique(cpus.begin(), cpus.end()) == cpus.end());
    #endif
    LOG(WARNING) << block_end_str() << "Configure multi-threading environment";
}

/**
 * Alloc an array. The i-th segment, i.e. [socket_array_end[i], socket_array_end[i+1])
 * is binded to i-th socket.
 */
template<typename T>
T * numa_alloc_array(size_t *socket_array_end, int numa_socket_num) {
	char * array = (char *)mmap(NULL, sizeof(T) * socket_array_end[numa_socket_num - 1], PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK(array!=NULL);
	for (int s_i = 0; s_i < numa_socket_num; s_i++) {
		size_t begin = (s_i == 0 ? 0 : socket_array_end[s_i - 1]);
		size_t end = socket_array_end[s_i];
		if (end > begin) {
			numa_tonode_memory(array + sizeof(T) * begin, sizeof(T) * (end - begin), s_i);
		}
	}
	return (T*)array;
}

/**
 * Free an array that is allocted by a NUMA function.
 */
template<typename T>
void numa_free_array(T* array, size_t *socket_array_end, int numa_socket_num) {
	if (array != NULL) {
		numa_free(array, sizeof(T) * socket_array_end[numa_socket_num - 1]);
	}
}
/**
 * Allocate an array in an interlevaved way.
 */
template<typename T>
T * numa_alloc_interleaved_array(size_t num) {
    T * array = (T *)numa_alloc_interleaved( sizeof(T) * num);
    CHECK(array!=NULL);
    return array;
}

/**
 * Free a NUMA array that is allocted in an interleaved way.
 */
template<typename T>
T * numa_dealloc_interleaved_array(T * array, size_t num) {
    numa_free(array, sizeof(T) * num);
}
