#pragma once

#include <assert.h>

#include "graph.hpp"
#include "sampler.hpp"
#include "walker.hpp"
#include "message.hpp"
#include "profiler.hpp"
#include "log.hpp"
#include "type.hpp"

/**
 * WalkManager do walks for shuffled walkers.
 */
class WalkManager {
    // graph, sm, and gen are not owned by this class
    Graph *graph;
    SamplerManager *sm;
    MessageManager *msgm;
    default_rand_t** rands;
    SampleProfiler *profiler;
    MultiThreadConfig mtcfg;

    // Varaibles for node2vec
    real_t p;
    real_t q;
    real_t n2v_lowerbound;
    real_t n2v_min_1_q;
    real_t n2v_min_1_p;
    real_t n2v_upperbound;
    real_t div_p;
    real_t div_q;
    bool is_node2vec;

public:
    WalkManager (MultiThreadConfig _mtcfg) {
        mtcfg = _mtcfg;
        is_node2vec = false;
    }

    void init(Graph *_graph, SamplerManager *_sm, MessageManager *_msgm, default_rand_t** _rands, SampleProfiler *_profiler) {
        graph = _graph;
        sm = _sm;
        msgm = _msgm;
        rands = _rands;
        profiler = _profiler;
    }

    /**
     * set_node2vec: Mark the walk to be node2vec, but don't prepare or initiate related data structure
     */
    void set_node2vec(real_t _p, real_t _q) {
        p = _p;
        q = _q;
        n2v_lowerbound = std::min((real_t) 1.0, std::min(1 / p, 1 / q));
        n2v_upperbound = std::max((real_t) 1.0, std::max(1 / p, 1 / q));
        n2v_min_1_p = std::min(1.0, 1.0 / p);
        n2v_min_1_q = std::min(1.0, 1.0 / q);
        div_p = 1.0 / p;
        div_q = 1.0 / q;
        is_node2vec = true;
    }

    /**
     * walk_message: Do static walks for a group of walkers that are currently at the same partition.
     */
    template<typename sampler_t>
    void walk_message(sampler_t *sampler, vertex_id_t *message_begin, vertex_id_t *message_end) {
        auto *rd = this->rands[omp_get_thread_num()];
        for (vertex_id_t *msg = message_begin; msg < message_end; msg ++) {
            *msg = sampler->sample(*msg, rd);
            assert(*msg < graph->v_num);
        }
    }

    /**
     * node2vec_accept: Decide if the edge is accepted for rejection sampling.
     */
    bool node2vec_accept(vertex_id_t previous_vertex, vertex_id_t current_vertex, vertex_id_t next_vertex, real_t prob, int socket) {
        if (previous_vertex == next_vertex) {
            return prob <= div_p;
        }
        if (prob <= n2v_min_1_q) {
            return true;
        }
        real_t val;
        if (graph->has_neighbor(previous_vertex, next_vertex, socket)) {
            val = 1.0;
        } else {
            val = div_q;
        }
        return prob <= val;
    }

    /**
     * node2vec_walk_message: Do node2vec walks for a group of walkers that are currently at the same partition.
     *
     * TODO: Large amount of random memory access dependency found here. Mitigating this will greatly improve the
     * performance.
     */
    template<typename sampler_t>
    void node2vec_walk_message(sampler_t *sampler, vertex_id_t *message_begin, vertex_id_t *state_begin, walker_id_t walker_num, int socket) {
        auto *rd = this->rands[omp_get_thread_num()];
        for (walker_id_t w_i = 0; w_i < walker_num; w_i++) {
            vertex_id_t &current_vertex = message_begin[w_i];
            vertex_id_t previous_vertex = state_begin[w_i];
            vertex_id_t next_vertex;
            real_t prob;
            do {
                next_vertex = sampler->sample(current_vertex, rd);
                assert(next_vertex < graph->v_num);
                prob = rd->gen_float(n2v_upperbound);
            } while (!node2vec_accept(previous_vertex, current_vertex, next_vertex, prob, socket));
            assert(current_vertex != next_vertex);
            current_vertex = next_vertex;
        }
    }

    /**
     * walk_message_dispatch: Find out correct sampler class for the static walk task.
     */
    void walk_message_dispatch(int p_i, vertex_id_t *message_begin, vertex_id_t *message_end) {
        auto *sampler = sm->samplers[p_i];
        if (sampler->sampler_class == ClassExclusiveBufferSampler) {
            walk_message(static_cast<ExclusiveBufferSampler*>(sampler), message_begin, message_end);
        } else if (sampler->sampler_class == ClassDirectSampler) {
            walk_message(static_cast<DirectSampler*>(sampler), message_begin, message_end);
        } else if (sampler->sampler_class == ClassUniformDegreeDirectSampler) {
            walk_message(static_cast<UniformDegreeDirectSampler*>(sampler), message_begin, message_end);
        } else if (sampler->sampler_class == ClassSimilarDegreeDirectSampler) {
            walk_message(static_cast<SimilarDegreeDirectSampler*>(sampler), message_begin, message_end);
        } else {
            CHECK(false);
        }
    }

    /**
     * node2vec_walk_message_dispatch: Find out correct sampler class for the node2vec walk task.
     */
    void node2vec_walk_message_dispatch(int p_i, vertex_id_t *message_begin, vertex_id_t *state_begin, walker_id_t message_num) {
        auto socket = graph->partition_socket[p_i];
        auto *sampler = sm->samplers[p_i];
        if (sampler->sampler_class == ClassExclusiveBufferSampler) {
            node2vec_walk_message(static_cast<ExclusiveBufferSampler*>(sampler), message_begin, state_begin, message_num, socket);
        } else if (sampler->sampler_class == ClassDirectSampler) {
            node2vec_walk_message(static_cast<DirectSampler*>(sampler), message_begin, state_begin, message_num, socket);
        } else if (sampler->sampler_class == ClassUniformDegreeDirectSampler) {
            node2vec_walk_message(static_cast<UniformDegreeDirectSampler*>(sampler), message_begin, state_begin, message_num, socket);
        } else if (sampler->sampler_class == ClassSimilarDegreeDirectSampler) {
            node2vec_walk_message(static_cast<SimilarDegreeDirectSampler*>(sampler), message_begin, state_begin, message_num, socket);
        } else {
            CHECK(false);
        }
    }

    /**
     * walk: All walkers walk one step. Note that even it's set to
     * be node2vec, the first step is just static walk. Thus the
     * first parameter is neccessary. When do walk tasks, half threads
     * do walks from high degree partitions to low degree partitions,
     * and the other half threads works at opposite order.
     */
    void walk(bool node2vec_walk, walker_id_t walker_num) {
        _unused(walker_num);
        Timer timer;
        double thread_time = 0;
        const auto _partition_num = graph->partition_num;
        _unused(_partition_num);
        profiler->walk_step++;
        std::vector<int> partittion_progress(mtcfg.socket_num, 0);
        std::vector<int> hdv_partition_progress(mtcfg.socket_num, 0);
        std::vector<int> ldv_partition_progress(mtcfg.socket_num, 0);

        #pragma omp parallel reduction(+: thread_time)
        {
            int worker_id = omp_get_thread_num();
            int socket = mtcfg.socket_id(worker_id);
            bool hdv_thread = mtcfg.socket_offset(worker_id) % 2;
            int progress_i;
            Timer thread_timer;
            while((progress_i =  __sync_fetch_and_add(&partittion_progress[socket], 1)) < graph->socket_partition_nums[socket]) {
                #if PROFILE_IF_NORMAL
                Timer partition_timer;
                #endif
                int p_i;
                if (hdv_thread) {
                    p_i = graph->socket_partitions[socket][__sync_fetch_and_add(&hdv_partition_progress[socket], 1)];
                } else {
                    p_i = graph->socket_partitions[socket][graph->socket_partition_nums[socket] - __sync_fetch_and_add(&ldv_partition_progress[socket], 1) - 1];
                }
                walker_id_t task_message_num = 0;

                int socket_threads = mtcfg.socket_thread_num();
                for (int s_i = 0; s_i < mtcfg.socket_num; s_i++) {
                    for (int t_i = 0; t_i < socket_threads; t_i++) {
                        auto mt = msgm->mtasks[s_i][t_i];
                        auto messages = mt->shuffled_messages + mt->shuffled_message_begin[p_i];
                        walker_id_t block_msg_num = mt->shuffled_message_end[p_i] - mt->shuffled_message_begin[p_i];
                        task_message_num += block_msg_num;
                        if (!node2vec_walk) {
                            walk_message_dispatch(p_i, messages, messages + block_msg_num);
                        } else {
                            auto states = mt->shuffled_states + mt->shuffled_message_begin[p_i];
                            node2vec_walk_message_dispatch(p_i, messages, states, block_msg_num);
                        }
                        /*
                        for (walker_id_t i = 0; i < block_msg_num; i++) {
                            assert(graph->partition_begin[p_i] <= messages[i] && messages[i] < graph->partition_end[p_i]);
                        }
                        */
                    }
                }

                #if PROFILE_IF_NORMAL
                uint64_t time_val = sec2ns(partition_timer.duration());
                auto group = graph->get_partition_group_id(p_i);
                __sync_fetch_and_add(&profiler->group_walk_time[group], time_val);
                __sync_fetch_and_add(&profiler->group_walker_num[group], task_message_num);
                #endif

                #if PROFILE_IF_NORMAL
                __sync_fetch_and_add(&profiler->partition_walk_time[p_i], time_val);
                __sync_fetch_and_add(&profiler->partition_walker_num[p_i], task_message_num);
                #endif
            }
            thread_time += thread_timer.duration();
        }

        #if PROFILE_IF_BRIEF
        profiler->sub_step_sync_times["3-Walk"] += timer.duration();
        profiler->sub_step_thread_times["3-Walk"] += thread_time / mtcfg.thread_num;
        #endif

        #if PROFILE_IF_DETAIL
        LOG(INFO) << "\tt2 (sample and walk): " << get_step_cost(timer.duration(), walker_num, mtcfg.thread_num) << " ns/step (thread time: " << get_step_cost(thread_time / mtcfg.thread_num, walker_num, mtcfg.thread_num) << " ns/step)";
        #endif
    }
};
