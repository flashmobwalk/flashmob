#pragma once

#include <functional>

#include "timer.hpp"
#include "io.hpp"
#include "util.hpp"
#include "graph.hpp"
#include "sampler.hpp"
#include "mini_bmk.hpp"

/**
 * Produce partition hint for a graph.
 *
 * The partition hint is produced based on mini benchmark results.
 * The partitioning is reduced to MCKP problem and is solved by
 * dynamic programming. Only partitions of size between
 * (1<<min_partition_vertex_bit) and (1<<max_partition_vertex_bit)
 * are taken into consideration. The #partitions in each level must
 * be no more than max_shuffle_partition_num.
 *
 */
void dp(
    double walker_per_edge,
    vertex_id_t min_partition_vertex_bit,
    vertex_id_t max_partition_vertex_bit,
    vertex_id_t max_shuffle_partition_num,
    std::map<vertex_id_t, std::map<vertex_id_t, std::vector<SampleEstimation> > > &costs,
    Graph* graph,
    GraphHint *graph_hint
)
{
    struct DPGroup {
        double val;
        vertex_id_t candidate_idx;
        DPGroup *previous;
    };

    LOG(WARNING) << block_begin_str(1) << "MCKP";
    Timer timer;
    auto &group_bits = graph_hint->group_bits;
    auto &group_hints = graph_hint->group_hints;
    auto &partition_sc= graph_hint->partition_sampler_class;
    auto &group_num = graph_hint->group_num;

    auto get_edge_num = [&] (vertex_id_t begin, vertex_id_t end) {
        return graph->degree_prefix_sum[end] - graph->degree_prefix_sum[begin];
    };
    auto get_walker_num = [&] (vertex_id_t begin, vertex_id_t end) {
        return get_edge_num(begin, end) * walker_per_edge;
    };

    Timer pre_timer;
    std::vector<std::vector<std::vector<SamplerClass> > > candidate_partition_sc(group_num);
    std::vector<std::vector<GroupHint> > candidate_group_hints(group_num);
    #pragma omp parallel for
    for (vertex_id_t g_i = 0; g_i < group_num; g_i++) {
        vertex_id_t group_vertex_begin = g_i << group_bits;
        vertex_id_t group_vertex_end = std::min(graph->v_num, (g_i + 1) << group_bits);

        for (vertex_id_t partition_vertex_bits = min_partition_vertex_bit; partition_vertex_bits <= max_partition_vertex_bit; partition_vertex_bits ++ ){
            CHECK(partition_vertex_bits <= group_bits) << partition_vertex_bits << " " << group_bits;
            if (costs[partition_vertex_bits].size() == 0) {
                continue;
            }
            GroupHint hint;
            std::vector<SamplerClass> group_sc;
            hint.partition_bits = partition_vertex_bits;
            hint.partition_num = (group_vertex_end - group_vertex_begin  + (1u << partition_vertex_bits) - 1u) >> partition_vertex_bits;
            hint.vertex_begin = group_vertex_begin;
            hint.vertex_end = group_vertex_end;
            hint.total_time = 0;
            auto &group_methods = costs[partition_vertex_bits];

            for (vertex_id_t p_i = 0; p_i < hint.partition_num; p_i++) {
                vertex_id_t partition_vertex_begin = group_vertex_begin + (1u << partition_vertex_bits) * p_i;
                vertex_id_t partition_vertex_end = std::min(group_vertex_end, partition_vertex_begin + (1u << partition_vertex_bits));

                edge_id_t partition_edge_num;
                double partition_walker_num;
                vertex_id_t avg_degree;
                if (g_i == 0 && p_i < max_shuffle_partition_num) {
                    vertex_id_t shuffle_vertex_begin = 0;
                    vertex_id_t shuffle_vertex_end = std::min(group_vertex_end, (1u << partition_vertex_bits) * max_shuffle_partition_num);
                    edge_id_t shuffle_edge_num = get_edge_num(shuffle_vertex_begin, shuffle_vertex_end);
                    double shuffle_walker_num = get_walker_num(shuffle_vertex_begin, shuffle_vertex_end);
                    partition_edge_num = shuffle_edge_num / (double) (shuffle_vertex_end - shuffle_vertex_begin) * (partition_vertex_end - partition_vertex_begin);
                    partition_walker_num = shuffle_walker_num / (double) (shuffle_vertex_end - shuffle_vertex_begin) * (partition_vertex_end - partition_vertex_begin);
                    avg_degree = shuffle_edge_num / (shuffle_vertex_end - shuffle_vertex_begin);
                } else {
                    partition_edge_num = get_edge_num(partition_vertex_begin, partition_vertex_end);
                    partition_walker_num = get_walker_num(partition_vertex_begin, partition_vertex_end);
                    avg_degree = partition_edge_num / (partition_vertex_end - partition_vertex_begin);
                }

                auto iter = group_methods.lower_bound(avg_degree);
                // The performance of direct sampling on partitions whose edges are larger than any
                // mini-benchmark results are inferred by adding penalty to the result that has the
                // closest edge number.
                double ds_penalty = 1;
                if (iter == group_methods.end()) {
                    iter --;
                    ds_penalty = avg_degree / iter->first;
                }

                auto &partition_methods = iter->second;

                // If a partition has too many walkers, there might be the case that many threads wait
                // one thread to finish its work. This can be mitigated by enabling work stealing. But
                // a simpler solution is to add penalty to such huge partition.
                double sync_penalty = 1;
                edge_id_t thread_max_work = std::max(1ul, graph->e_num / (uint32_t)omp_get_num_threads() / 8u);
                if (partition_edge_num > thread_max_work) {
                    sync_penalty = (double) partition_edge_num / thread_max_work;
                }

                SamplerClass partition_sc;
                double partition_val = -1;
                for (auto &method : partition_methods) {
                    double val = method.step_time * partition_walker_num;
                    if (method.sampler_class != ClassExclusiveBufferSampler) {
                        val *= ds_penalty;
                    }
                    val *= sync_penalty;
                    if (partition_val < 0 || partition_val > val) {
                        partition_val = val;
                        partition_sc = method.sampler_class;
                    }
                }
                hint.total_time += partition_val;
                group_sc.push_back(partition_sc);
            }

            CHECK(hint.partition_bits <= group_bits) << "Pre-processing " << hint.partition_bits << " " << group_bits;
            hint.partition_level = 0;
            hint.step_time = hint.total_time / get_walker_num(group_vertex_begin, group_vertex_end);
            candidate_group_hints[g_i].push_back(hint);
            candidate_partition_sc[g_i].push_back(group_sc);

            // The following code will add second level partitioning into the DP model.
            // Temporarily disable this to avoid a numa-allocation bug: when there are
            // too many partitions, the numa_alloc_* functions will fail. The root cause
            // is that there are only certain amount of memories that can be allocated via
            // the above functions for each process. And FlashMob SamplerManager allocates
            // a memory for each partition. To fix in the future.
            // double shuffle_overhead = 14;
            // hint.partition_level = 1;
            // hint.total_time += get_walker_num(group_vertex_begin, group_vertex_end) * shuffle_overhead;
            // hint.step_time = hint.total_time / get_walker_num(group_vertex_begin, group_vertex_end);
            // candidate_group_hints[g_i].push_back(hint);
            // candidate_partition_sc[g_i].push_back(group_sc);
        }
    }

    LOG(WARNING) << block_mid_str(1) << "Pre-processing in " << pre_timer.duration() << " seconds";

    Timer dp_timer;
    vertex_id_t P = max_partition_num;
    vertex_id_t G = group_num;
    std::vector<std::vector<DPGroup> > f(G + 1);
    for (auto &vec : f) {
        vec.resize(P + 1);
        for (auto &val : vec) {
            val.val = -1;
            val.previous = nullptr;
        }
    }
    for (auto &val : f[0]) {
        val.val = 0;
    }

    for (vertex_id_t g_i = 1; g_i <= G; g_i++) {
        for (vertex_id_t candidate_idx = 0; candidate_idx < candidate_group_hints[g_i - 1].size(); candidate_idx++) {
            auto &hint = candidate_group_hints[g_i - 1][candidate_idx];
            CHECK(hint.partition_bits <= group_bits) << "dp " << hint.partition_bits << " " << group_bits << " " << g_i - 1 << " " << candidate_idx;
            vertex_id_t weight = (hint.partition_level == 0 ? hint.partition_num : 1);
            for (vertex_id_t p_i = weight; p_i <= P; p_i++) {
                auto &current = f[g_i][p_i];
                auto &previous = f[g_i - 1][p_i - weight];
                if (previous.val < 0) {
                    continue;
                }
                if (current.val < 0 || current.val > previous.val + hint.total_time) {
                    current.val = previous.val + hint.total_time;
                    current.candidate_idx = candidate_idx;
                    current.previous = &previous;
                }
            }
        }
        for (vertex_id_t p_i = 1; p_i <= P; p_i++) {
            if (f[g_i][p_i - 1].val >= 0 && (f[g_i][p_i].val < 0 || f[g_i][p_i].val > f[g_i][p_i].val)) {
                f[g_i][p_i] = f[g_i][p_i - 1];
            }
        }
    }

    std::vector<DPGroup> results(G);
    DPGroup *dp_p = &f[G][P];
    for (vertex_id_t g_i = G; g_i >= 1; g_i --) {
        CHECK(dp_p != nullptr);
        results[g_i - 1] = *dp_p;
        dp_p = dp_p->previous;
    }

    for (vertex_id_t g_i = 0; g_i < group_num; g_i++) {
        group_hints.push_back(candidate_group_hints[g_i][results[g_i].candidate_idx]);
        for (auto sc : candidate_partition_sc[g_i][results[g_i].candidate_idx]) {
            partition_sc.push_back(sc);
        }
    }

    LOG(WARNING) << block_mid_str(1) << "DP in " << dp_timer.duration() << " seconds";
    LOG(WARNING) << block_end_str(1) << "MCKP in " << timer.duration() << " seconds";
}

void get_partition_hint(double walker_per_edge, Graph *graph, MultiThreadConfig mtcfg, GraphHint *graph_hint) {
    auto &group_bits = graph_hint->group_bits;
    auto &group_hints = graph_hint->group_hints;
    auto &partition_sampler_class = graph_hint->partition_sampler_class;
    auto &group_num = graph_hint->group_num;

    group_bits = 0;
    while ((graph->v_num >> group_bits) > max_group_num) {
        group_bits ++;
    }
    group_num = (graph->v_num + (1u << group_bits) - 1) / (1u << group_bits);
#ifndef UNIT_TEST
    _unused(group_hints);
    _unused(partition_sampler_class);
    vertex_id_t min_partition_vertex_bit = std::min((uint32_t)min_partition_bits, group_bits);
    vertex_id_t max_partition_vertex_bit = std::min(24u, group_bits);
    std::map<vertex_id_t, std::map<vertex_id_t, std::vector<SampleEstimation> > > costs;
    vertex_id_t max_benchmark_degree = 2048;
    LOG(INFO) << block_mid_str() << "Max benchmark degree: " << max_benchmark_degree;
    mini_benchmark(walker_per_edge, max_benchmark_degree, min_partition_vertex_bit, max_partition_vertex_bit, mtcfg, costs);

    dp(walker_per_edge, min_partition_vertex_bit, max_partition_vertex_bit, mtcfg.thread_num, costs, graph, graph_hint);
#else
    group_hints.resize(group_num);
    vertex_id_t partition_num = 0;
    for (vertex_id_t g_i = 0; g_i < group_hints.size(); g_i++) {
        auto &hint = group_hints[g_i];
        hint.vertex_begin = g_i << group_bits;
        hint.vertex_end = std::min((g_i + 1u) << group_bits, graph->v_num);
        hint.partition_bits = group_bits;
        hint.total_time = 0;
        partition_num++;
    }
    for (size_t g_i = 0; g_i < group_hints.size(); g_i++) {
        auto &hint = group_hints[g_i];
        while (rand() % 2 == 0 && hint.partition_bits > 0) {
            vertex_id_t old_group_partition_num = bit2value(group_bits - hint.partition_bits);
            vertex_id_t new_group_partition_num = bit2value(group_bits - (hint.partition_bits - 1));
            if (partition_num - old_group_partition_num + new_group_partition_num < max_partition_num) {
                hint.partition_bits --;
            }
            partition_num = partition_num - old_group_partition_num + new_group_partition_num;
        }
    }
    for (vertex_id_t p_i = 0; p_i < partition_num; p_i++) {
        partition_sampler_class.push_back(static_cast<SamplerClass>(rand() % ClassSamplerHintNum));
    }
#endif
}

uint64_t estimate_epoch_walker(
    vertex_id_t vertex_num,
    edge_id_t edge_num,
    edge_id_t buffer_edge_num,
    uint64_t walker_num,
    int walk_len,
    int socket_num,
    uint64_t mem_quota,
    size_t other_size = 0
)
{
    #ifdef UNIT_TEST
    uint64_t temp_max_epoch_walker_num = std::min((uint64_t)vertex_num * 2u, walker_num);
    #else
    size_t graph_memory_size = sizeof(AdjList) * vertex_num * (size_t) socket_num + sizeof(AdjUnit) * edge_num;
    size_t buffer_memory_size = sizeof(vertex_id_t) * buffer_edge_num;
    size_t per_walker_cost = sizeof(vertex_id_t) * (
    // walk paths
    (walk_len * 2) \
    // messages + starting vertices
    + 2 + 1);
    // LOG(WARNING) << block_mid_str() << "Estimated memory size for graph data: " << size_string(graph_memory_size + buffer_memory_size + other_size);
    CHECK(mem_quota > graph_memory_size + buffer_memory_size + other_size) << "Assigned memory is too small to continue the computation";
    auto cal_max_active_walker_num = [&] (size_t memory_size) {
        uint64_t val = (memory_size - graph_memory_size - buffer_memory_size - other_size) / per_walker_cost;
        return val;
    };
    auto cal_epoch_num = [&] (size_t memory_size) {
        uint64_t temp_max_epoch_walker_num = std::min(cal_max_active_walker_num(memory_size), (uint64_t) walker_num);
        uint64_t epoch_num = (walker_num + temp_max_epoch_walker_num - 1) / temp_max_epoch_walker_num;
        return epoch_num;
    };

    auto cal_max_epoch_walker_num = [&] (size_t memory_size) {
        auto epoch_num = cal_epoch_num(memory_size);
        uint64_t temp_max_epoch_walker_num = (walker_num + epoch_num - 1) / epoch_num;
        return temp_max_epoch_walker_num;
    };

    uint64_t temp_max_epoch_walker_num = cal_max_epoch_walker_num(mem_quota);
    #endif
    temp_max_epoch_walker_num = std::min(temp_max_epoch_walker_num, (1ul << sizeof(walker_id_t) * 8ul) - 2ul);
    return temp_max_epoch_walker_num;
}

/**
 * Load graph from the file. Next produce partition hints by mini-benchmark and MCKP.
 * Then partition the graph and make edge lists.
 */
void make_graph(
    const char* path,
    GraphFormat graph_format,
    bool as_undirected,
    std::function<uint64_t(vertex_id_t, edge_id_t)> walker_num_func,
    int walk_len,
    MultiThreadConfig mtcfg,
    uint64_t mem_quota,
    bool is_node2vec,
    Graph &graph
)
{

    Timer timer;
    LOG(WARNING) << block_begin_str() << "Initialize graph";
    graph.load(path, graph_format, as_undirected);

    uint64_t total_walker = walker_num_func(graph.v_num, graph.e_num);
    uint64_t epoch_walker = estimate_epoch_walker(graph.v_num, graph.e_num, graph.e_num, total_walker, walk_len, mtcfg.socket_num, mem_quota, is_node2vec ? BloomFilter::cal_hash_table_size(as_undirected ? graph.e_num / 2 : graph.e_num): 0);
    double walker_per_edge = (double)epoch_walker / graph.e_num;
    LOG(WARNING) << block_mid_str() << "walker_per_edge " << walker_per_edge;

    GraphHint graph_hint;
    get_partition_hint(walker_per_edge, &graph, mtcfg, &graph_hint);

    graph.make(&graph_hint);
    LOG(WARNING) << block_end_str() << "Initialize graph in " << timer.duration() << " seconds";
}
