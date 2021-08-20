#pragma once

#include <map>
#include <vector>

#include "log.hpp"
#include "compile_helper.hpp"

/**
 * SampleProfiler records execution statistics for performance profiling.
 */
struct SampleProfiler {
    // for profiling
    size_t edge_buffer_data_size;
    int walk_step;
    std::map<std::string, double> sub_step_thread_times;
    std::map<std::string, double> sub_step_sync_times;
    #if PROFILE_IF_NORMAL
    std::vector<uint64_t> group_walk_time;
    std::vector<uint64_t> group_walker_num;
    std::vector<uint64_t> group_vertex_num;
    #endif
    #if PROFILE_IF_NORMAL
    std::vector<uint64_t> partition_walk_time;
    std::vector<uint64_t> partition_walker_num;
    std::vector<uint64_t> partition_vertex_num;
    std::vector<uint64_t> partition_edge_num;
    std::vector<SamplerClass> partition_sampler_class;
    #endif

    // max item per line in the log
    int max_log_num;
    int log_step_len;

    SampleProfiler(int partition_num, int group_num) {
        _unused(group_num);
        max_log_num = 1000;
        log_step_len = (partition_num + max_log_num - 1) / max_log_num;

        edge_buffer_data_size = 0;
        walk_step = 0;
        #if PROFILE_IF_NORMAL
        group_walk_time.resize(group_num, 0);
        group_walker_num.resize(group_num, 0);
        group_vertex_num.resize(group_num, 0);
        #endif
        #if PROFILE_IF_NORMAL
        partition_walk_time.resize(partition_num, 0);
        partition_walker_num.resize(partition_num, 0);
        partition_vertex_num.resize(partition_num, 0);
        partition_edge_num.resize(partition_num, 0);
        partition_sampler_class.resize(partition_num, ClassBaseSampler);
        #endif
    }
};
