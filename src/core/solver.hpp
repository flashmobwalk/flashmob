#pragma once

#include <stdlib.h>
#include <stdio.h>

#include <iomanip>
#include <map>

#include <omp.h>

#include "constants.hpp"
#include "timer.hpp"
#include "random.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "graph.hpp"
#include "walker.hpp"
#include "message.hpp"
#include "sampler.hpp"
#include "walk.hpp"
#include "compile_helper.hpp"
#include "profiler.hpp"
#include "partition.hpp"
#include "perf_helper.hpp"

/**
 * FMobSolver manages the whole random walk processing.
 */
class FMobSolver{
    MultiThreadConfig mtcfg;
    Graph* graph;
    default_rand_t** rands;
    MemoryPool mpool;

    uint64_t rest_walker_num;
    uint64_t terminated_walker_num;
    uint64_t max_epoch_walker_num;
    double total_walk_time;
    unsigned walk_len;

    vertex_id_t *walker_start_vertices;
    walker_id_t walker_start_vertices_num;

    bool is_node2vec;

    MessageManager msgm;
    SamplerManager sm;
    WalkManager wm;
    WalkerManager wkrm;

    std::vector<vertex_id_t*> walks;

    bool is_hdv_thread (int t_id) {
        return (int)t_id < (mtcfg.thread_num + 1) / 2;
    }

    // Initialize the arrays that will store the paths
    void init_walks(walker_id_t num_walker, int walk_len) {
        CHECK(num_walker <= max_epoch_walker_num);
        if ((int) walks.size() < walk_len) {
            Timer timer;
            int old_num = walks.size();
            int new_num = walk_len - old_num;
            walks.resize(walk_len);
            #pragma omp parallel for
            for (int w_i = old_num; w_i < new_num; w_i++) {
                walks[w_i] = wkrm.alloc_walker_array<vertex_id_t>();
            }
            LOG(WARNING) << block_mid_str() << "Initialize walk arrays in " << timer.duration() << " seconds";
        }
    }

    vertex_id_t* get_walker_start_vertices(walker_id_t epoch_walker_num) {
        const vertex_id_t v_num = graph->v_num;
        if (walker_start_vertices_num < epoch_walker_num) {
            wkrm.dealloc_walker_array(walker_start_vertices);
            walker_start_vertices = wkrm.alloc_walker_array<vertex_id_t>();
        }
        walker_start_vertices_num = epoch_walker_num;
        wkrm.process_walkers([&](walker_id_t w_i) {
            walker_start_vertices[w_i] = rands[omp_get_thread_num()]->gen(v_num);
        }, epoch_walker_num);
        return walker_start_vertices;
    }

public:
    SampleProfiler profiler;

    FMobSolver(Graph* _graph, MultiThreadConfig _mtcfg) : mtcfg (_mtcfg), mpool(_mtcfg), msgm(_mtcfg), sm(_mtcfg), wm(_mtcfg), wkrm(_mtcfg), profiler(_graph->partition_num, _graph->group_num) {
        graph = _graph;
        is_node2vec = false;
        rands = nullptr;
    }

    ~FMobSolver() {
        for (auto walk : walks) {
            wkrm.dealloc_walker_array(walk);
        }
        if (rands != nullptr) {
            delete []rands;
        }
        if (walker_start_vertices != nullptr) {
            wkrm.dealloc_walker_array(walker_start_vertices);
        }
    }

    // Set node2vec, but don't prepare or initialize related data structure now.
    void set_node2vec(real_t _p, real_t _q) {
        is_node2vec = true;
        wm.set_node2vec(_p, _q);
    }

    std::string name() {
        return std::string("FlashMob solver");
    }

    void prepare(uint64_t _walker_num, int _walk_len, uint64_t mem_quota) {
        LOG(WARNING) << block_begin_str() << "Initialize Solver";
        Timer timer;

        rands = new default_rand_t*[mtcfg.thread_num];
        for (int t_i = 0; t_i < mtcfg.thread_num; t_i++) {
            // Don't use StdRandNumGenerator here,
            // as it does not support numa-aware allocation.
            rands[t_i] = mpool.alloc_new<default_rand_t>(1, mtcfg.socket_id(t_i));
        }
        LOG(WARNING) << block_mid_str() << "RandNumGenerator: " << rands[0]->name();

        rest_walker_num = 0;
        terminated_walker_num = 0;
        total_walk_time = 0;
        walk_len = 0;
        max_epoch_walker_num = 0;

        walker_start_vertices = nullptr;
        walker_start_vertices_num = 0;

        rest_walker_num = _walker_num;
        terminated_walker_num = 0;
        walk_len = _walk_len;

        if (is_node2vec) {
            graph->prepare_neighbor_query();
        }

        edge_id_t buffer_edge_num = 0;
#pragma omp parallel for reduction (+: buffer_edge_num)
        for (int p_i = 0; p_i < graph->partition_num; p_i++) {
            if (graph->partition_sampler_class[p_i] == ClassExclusiveBufferSampler) {
                for (vertex_id_t v_i = graph->partition_begin[p_i]; v_i < graph->partition_end[p_i]; v_i++) {
                    buffer_edge_num += graph->adjlists[0][v_i].degree;
                }
            }
        }
        size_t ht_size = is_node2vec ? graph->bf->size() : 0;
        uint64_t temp_max_epoch_walker_num = estimate_epoch_walker(graph->v_num, graph->e_num, buffer_edge_num, _walker_num, walk_len, mtcfg.socket_num, mem_quota, ht_size);
        std::stringstream epoch_walker_ss;
        int epoch_num = 0;
        for (uint64_t w_i = 0; w_i < _walker_num;) {
            uint64_t epoch_walker_num = std::min(temp_max_epoch_walker_num, _walker_num - w_i);
            epoch_walker_ss << " " << epoch_walker_num;
            w_i += epoch_walker_num;
            epoch_num ++;
        }
        #if PROFILE_IF_BRIEF
        LOG(INFO) << block_mid_str() << "Total walkers: " <<  _walker_num << ", max_epoch_walkers: " << temp_max_epoch_walker_num << ", total epochs: " << epoch_num;
        LOG(INFO) << block_mid_str() << "Epoch walkers: " << epoch_walker_ss.str();
        LOG(WARNING) << block_mid_str() << "Walker density: " << (double) temp_max_epoch_walker_num / graph->e_num;
        #endif
        max_epoch_walker_num = temp_max_epoch_walker_num;

        sm.init(graph, temp_max_epoch_walker_num, &profiler);
        wm.init(graph, &sm, &msgm, rands, &profiler);
        wkrm.init(temp_max_epoch_walker_num);
        msgm.init(graph, &wkrm, &profiler, is_node2vec);
        init_walks(temp_max_epoch_walker_num, _walk_len);

        LOG(WARNING) << block_end_str() << "Solver initialized in " << timer.duration() << " seconds";
    }

    void walk(vertex_id_t *output, walker_id_t &epoch_walker_num) {
        Timer timer;

        epoch_walker_num = std::min(max_epoch_walker_num, rest_walker_num);
        const walker_id_t _walker_num = epoch_walker_num;
        const int _walk_len = walk_len;

        init_walks(epoch_walker_num, _walk_len);

        auto *start_vertices = get_walker_start_vertices(_walker_num);
        vertex_id_t *current_vertices = walks[0];
        vertex_id_t *previous_vertices = nullptr;

        #pragma omp parallel for
        for (walker_id_t w_i = 0; w_i < _walker_num; w_i++) {
            current_vertices[w_i] = start_vertices[w_i];
        }

        #if PROFILE_IF_BRIEF
        profiler.sub_step_sync_times["0-Init"] += timer.duration();
        #endif

        // All walkers walk in lock step
        for (int l_i = 1; l_i < _walk_len; l_i++) {
            bool node2vec_walk = (is_node2vec && l_i != 0);

            #if PROFILE_IF_DETAIL
            Timer step_timer;
            LOG(INFO) << "step " << l_i << ":";
            #endif

            msgm.shuffle(current_vertices, node2vec_walk ? previous_vertices : nullptr, _walker_num);

            wm.walk(node2vec_walk, _walker_num);

            vertex_id_t *next_vertices = walks[l_i];
            msgm.update(next_vertices, _walker_num);

            previous_vertices = current_vertices;
            current_vertices = next_vertices;
            #if PROFILE_IF_DETAIL
            LOG(INFO) << "\tstep time: " << step_timer.duration() << "(" << timer.duration() << ") seconds, " << get_step_cost(step_timer.duration(), _walker_num, mtcfg.thread_num) << " ns/step";
            #endif
        }

        // Shuffle the paths to correct order
        #if PROFILE_IF_BRIEF
        Timer shuffle_timer;
        #endif
        wkrm.process_walkers([&](walker_id_t w_i) {
            for (int step_i = 0; step_i < _walk_len; step_i++) {
                output[(uint64_t)w_i * _walk_len + step_i] = walks[step_i][w_i];
            }
        }, _walker_num);
        #if PROFILE_IF_BRIEF
        profiler.sub_step_sync_times["5-Path"] += shuffle_timer.duration();
        #endif

        #if PROFILE_IF_DETAIL
        LOG(INFO) << "final shuffle: " << shuffle_timer.duration() << " (" << timer.duration() << ") seconds, " << get_step_cost(shuffle_timer.duration(), (uint64_t) _walker_num * _walk_len, mtcfg.thread_num) << " ns/step";
        #endif

        CHECK(rest_walker_num >= epoch_walker_num);
        terminated_walker_num += epoch_walker_num;
        rest_walker_num -= epoch_walker_num;
        total_walk_time += timer.duration();
    }

    void walk_info() {
        uint64_t terminated_walk_step = (uint64_t) walk_len * terminated_walker_num;
        #if PROFILE_IF_BRIEF
        // LOG(INFO) << "edges sampled vs edges used: " << (double) profiler.total_refilled_edge_num / profiler.total_used_edge_num;
        // LOG(INFO) << "edges buffer size vs edges used: " << (double) profiler.edge_buffer_data_size / profiler.total_used_edge_num;
        // LOG(INFO) << "edge buffer miss rate: " << (double) profiler.total_buffer_miss_num / get_terminated_walk_step();
        LOG(INFO) << split_line_string();
        LOG(INFO) << "Sync Time (time recording from start to the finishing of all threads):";
        std::stringstream sub_step_time_name_ss;
        std::stringstream sub_step_time_val_ss;
        std::stringstream sub_step_time_percent_ss;
        double sub_step_total_time = 0;
        for (auto &iter : profiler.sub_step_sync_times) {
            sub_step_total_time += iter.second;
        }
        sub_step_time_name_ss << "Phases";
        sub_step_time_val_ss << "Time";
        sub_step_time_percent_ss << "Percent";
        for (auto &iter : profiler.sub_step_sync_times) {
            sub_step_time_name_ss << "\t" << iter.first;
            sub_step_time_val_ss << "\t" << std::setprecision(5) << get_step_cost(iter.second, terminated_walk_step, mtcfg.thread_num);
            sub_step_time_percent_ss << "\t" << std::setprecision(5) << iter.second / sub_step_total_time * 100 << "%";
        }
        LOG(INFO) << sub_step_time_name_ss.str();
        LOG(INFO) << sub_step_time_val_ss.str();
        LOG(INFO) << sub_step_time_percent_ss.str();

        LOG(INFO) << split_line_string();
        LOG(INFO) << "Thread Time (The sum of the time elapsing of each thread):";
        sub_step_time_name_ss.str("");
        sub_step_time_val_ss.str("");
        sub_step_time_percent_ss.str("");
        sub_step_time_name_ss << "Phases";
        sub_step_time_val_ss << "Time";
        sub_step_time_percent_ss << "Percent";
        for (auto &iter : profiler.sub_step_thread_times) {
            sub_step_time_name_ss << "\t" << iter.first;
            sub_step_time_val_ss << "\t" << std::setprecision(5) << get_step_cost(iter.second, terminated_walk_step, mtcfg.thread_num);
            sub_step_time_percent_ss << "\t" << std::setprecision(5) << iter.second / sub_step_total_time * 100 << "%";
        }
        LOG(INFO) << sub_step_time_name_ss.str();
        LOG(INFO) << sub_step_time_val_ss.str();
        LOG(INFO) << sub_step_time_percent_ss.str();
        LOG(INFO) << split_line_string();
        #endif

        #if PROFILE_IF_NORMAL
        LOG(INFO) << split_line_string();
        LOG(INFO) << "Pid\tGid\tSampler\tPbit\tdegree\tSample\tWalker";
        for (int p_i = 0; p_i < graph->partition_num; p_i++) {
            std::stringstream pt_ss;
            pt_ss << std::setprecision(3);
            pt_ss << p_i;
            auto g_i = graph->get_partition_group_id(p_i);
            pt_ss << "\t" << g_i;
            auto &hint = graph->group_hints[g_i];
            if (profiler.partition_sampler_class[p_i] == ClassExclusiveBufferSampler) {
                pt_ss << "\t" << "PS";
            } else if (profiler.partition_sampler_class[p_i] == ClassUniformDegreeDirectSampler) {
                pt_ss << "\t" << "UDS";
            } else if (profiler.partition_sampler_class[p_i] == ClassSimilarDegreeDirectSampler) {
                pt_ss << "\t" << "SDS";
            } else {
                pt_ss << "\t" << "DS";
            }
            pt_ss << "\t" << hint.partition_bits;
            pt_ss << "\t" << (double) profiler.partition_edge_num[p_i] / profiler.partition_vertex_num[p_i];
            double walk_val = (profiler.partition_walker_num[p_i] == 0 ? 0.0 : (double) profiler.partition_walk_time[p_i] / profiler.partition_walker_num[p_i]);
            pt_ss << "\t" << walk_val;
            double walker_val = profiler.partition_vertex_num[p_i] == 0 ? 0 : (double) profiler.partition_walker_num[p_i] / profiler.partition_vertex_num[p_i] / profiler.walk_step;
            pt_ss << "\t" << walker_val;
            LOG(INFO) << pt_ss.str();
        }
        #endif

#ifdef PROFILE_BF
        if (is_node2vec) {
            uint64_t qhit = graph->bf->qhit_counter;
            uint64_t qmiss = graph->bf->qmiss_counter;
            LOG(WARNING) << "BloomFilter: hit " << (double) qhit / terminated_walk_step << ", miss " << (double) qmiss / terminated_walk_step << ", hit rate " << (qhit == 0 ? (double) 0.0 : (double) qhit / (qhit + qmiss));
        }
#endif

        LOG(WARNING) << "time: " << total_walk_time << " s" \
            << ", step: " << number_string(terminated_walk_step) \
            << ", throughput: " << number_string(terminated_walk_step / total_walk_time) << "/s" \
            << ", speed: " << get_step_cost(total_walk_time, terminated_walk_step, mtcfg.thread_num) << " ns";
    }

    vertex_id_t* alloc_output_array() {
        return wkrm.alloc_walker_array<vertex_id_t>(walk_len);
    }

    void dealloc_output_array(vertex_id_t* walks) {
        wkrm.dealloc_walker_array(walks, walk_len);
    }

    bool has_next_walk() {
        return (rest_walker_num != 0);
    }
};

void walk(FMobSolver * solver, uint64_t walker_num, int walk_len, uint64_t mem_quota) {
    LOG(WARNING) << split_line_string();

    solver->prepare(walker_num, walk_len, mem_quota);
    // LOG(WARNING) << "Sampler: " << solver->name();
    vertex_id_t *walks = solver->alloc_output_array();

    System::profile("sample", [&]() {
        uint64_t terminated_walker_num = 0;
        while (solver->has_next_walk()) {
            walker_id_t epoch_walker_num;
            solver->walk(walks, epoch_walker_num);
            terminated_walker_num += epoch_walker_num;
        }
        CHECK(terminated_walker_num == walker_num);
        solver->walk_info();
    });
    solver->dealloc_output_array(walks);
}
