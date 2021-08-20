#pragma once

#include <assert.h>

#include "memory.hpp"
#include "log.hpp"
#include "graph.hpp"
#include "profiler.hpp"
#include "walker.hpp"

/**
 * MessageTask does shuffling and updating for a sub-array of walkers.
 * The message[i][j] will first be shuffled to partition[message[i][j]],
 * and then be written back to message[i+1][j].
 */
struct MessageTask {
    Graph* graph;
    int socket;
    vertex_id_t partition_num;
    walker_id_t origin_message_begin;
    walker_id_t origin_message_end;
    walker_id_t *shuffled_message_begin; // vertex_id_t[partition_num]
    walker_id_t *shuffled_message_end; // vertex_id_t[partition_num]
    vertex_id_t *shuffled_messages; // vertex_id_t[origin_message_(end - begin) + alignement * parition_num]
    walker_state_t *shuffled_states; // vertex_id_t[origin_message_(end - begin) + alignement * parition_num]
    partition_id_t *partition_ids; // partition_id_t[origin_message_begin .. origin_message_end]

    /**
     * The value of all these variables are given in the MessageManager.
     */
    MessageTask() {
        graph = nullptr;
        partition_num = 0;
        origin_message_begin = 0;
        origin_message_end = 0;
        shuffled_message_begin = nullptr;
        shuffled_message_end = nullptr;
        shuffled_messages = nullptr;
        shuffled_states = nullptr;
        partition_ids = nullptr;
    }

    /**
     * prepare: Counting for each partition how many messages will be
     * sent to by this MessageTask instance. Then use this information
     * to calculate the begining position of the messages in each partition.
     */
    void prepare (vertex_id_t *origin_messages)
    {
        for (vertex_id_t p_i = 0; p_i < partition_num; p_i++) {
            shuffled_message_end[p_i] = 0;
        }
        const vertex_id_t group_bits = graph->group_bits;
        const vertex_id_t group_mask = graph->group_mask;
        GroupHeader *gh = graph->groups[socket];
        for (walker_id_t m_i = origin_message_begin; m_i < origin_message_end; m_i++) {
            vertex_id_t msg = origin_messages[m_i];
            vertex_id_t group_id = msg >> group_bits;
            partition_id_t p_i = ((msg & group_mask) >> gh[group_id].partition_bits) + gh[group_id].partition_offset;
            assert(p_i < partition_num);
            partition_ids[m_i] = p_i;
            shuffled_message_end[p_i] ++;
        }
        walker_id_t counter = 0;
        for (vertex_id_t p_i = 0; p_i < partition_num; p_i++) {
            shuffled_message_begin[p_i] = counter;
            counter += shuffled_message_end[p_i];
        }
        for (vertex_id_t p_i = 0; p_i < partition_num; p_i++) {
            shuffled_message_end[p_i] = shuffled_message_begin[p_i];
        }
    }

    /**
     * shuffle: Send messages and its associated states if any, to their
     * destination partitions.
     */
    void shuffle(vertex_id_t *origin_messages, walker_state_t *origin_states)
    {
        if (origin_states == nullptr) {
            for (walker_id_t m_i = origin_message_begin; m_i < origin_message_end; m_i++) {
                partition_id_t p_i = partition_ids[m_i];
                vertex_id_t shuffled_i = shuffled_message_end[p_i] ++;
                shuffled_messages[shuffled_i] = origin_messages[m_i];
            }
        } else {
            for (walker_id_t m_i = origin_message_begin; m_i < origin_message_end; m_i++) {
                partition_id_t p_i = partition_ids[m_i];
                vertex_id_t shuffled_i = shuffled_message_end[p_i] ++;
                shuffled_messages[shuffled_i] = origin_messages[m_i];
                shuffled_states[shuffled_i] = origin_states[m_i];
            }
        }
    }

    /**
     * update: Write the updated messages back to the walkers.
     */
    void update (vertex_id_t *target_messages)
    {
        for (walker_id_t m_i = origin_message_begin; m_i < origin_message_end; m_i++) {
            partition_id_t p_i = partition_ids[m_i];
            walker_id_t shuffled_i = shuffled_message_begin[p_i]++;
            target_messages[m_i] = shuffled_messages[shuffled_i];
        }
    }
};

/**
 * MessageMananger allocates arrays and schedules shuffling tasks
 * in a NUMA-aware manner.
 */
class MessageManager
{
    MultiThreadConfig mtcfg;

    MemoryPool mpool;
    bool is_node2vec;
    partition_id_t *partition_ids;

    // shared members, not owned.
    Graph *graph;
    SampleProfiler *profiler;
    WalkerManager *wkrm;

public:
    std::vector<MessageTask**> mtasks;
    int num_lv1_task;
    vertex_id_t lv0_partition_bits;

    ~MessageManager() {
        mpool.clear();
        if (partition_ids != nullptr) {
            wkrm->dealloc_walker_array(partition_ids);
        }
    }

    MessageManager(MultiThreadConfig _mtcfg) : mpool(_mtcfg) {
        mtcfg = _mtcfg;
        num_lv1_task = 0;
        lv0_partition_bits = 0;
        partition_ids = nullptr;

        graph = nullptr;
        wkrm = nullptr;
        profiler = nullptr;
    }

    void init(Graph* _graph, WalkerManager *_wkrm, SampleProfiler *_profiler, bool _is_node2vec) {
        Timer timer;
        graph = _graph;
        wkrm = _wkrm;
        profiler = _profiler;
        is_node2vec = _is_node2vec;

        mtasks.resize(mtcfg.socket_num, nullptr);
        partition_ids = wkrm->alloc_walker_array<partition_id_t>();
        for (int s_i = 0; s_i < mtcfg.socket_num; s_i++) {
            int socket_threads = mtcfg.socket_thread_num();
            // walker_id_t socket_message_num = wkrm->socket_walker_end[s_i] - wkrm->socket_walker_begin[s_i];

            // count memory allocation
            MemoryCounter mc;
            mc.al_alloc<MessageTask*>(socket_threads);
            for (int t_i = 0; t_i < socket_threads; t_i++) {
                mc.al_alloc<MessageTask>();
                mc.al_alloc<walker_id_t>(graph->partition_num);
                mc.al_alloc<walker_id_t>(graph->partition_num);
                auto origin_message_begin = wkrm->thread_walker_begin[s_i][t_i];
                auto origin_message_end = wkrm->thread_walker_end[s_i][t_i];
                mc.al_alloc<vertex_id_t>(origin_message_end - origin_message_begin);
                if (is_node2vec) {
                    mc.al_alloc<vertex_id_t>(origin_message_end - origin_message_begin);
                }
                mc.align();
            }

            // memory allocation
            Memory* m = mpool.get_memory(&mc, s_i);
            mtasks[s_i] = m->al_alloc_new<MessageTask*>(socket_threads);
            for (int t_i = 0; t_i < socket_threads; t_i++) {
                auto &mt = mtasks[s_i][t_i];
                mt = m->al_alloc_new<MessageTask>();
                mt->graph = graph;
                mt->partition_num = graph->partition_num;
                mt->socket = s_i;
                mt->origin_message_begin = wkrm->thread_walker_begin[s_i][t_i];
                mt->origin_message_end = wkrm->thread_walker_end[s_i][t_i];
                mt->shuffled_message_begin = m->al_alloc<walker_id_t>(graph->partition_num);
                mt->shuffled_message_end = m->al_alloc<walker_id_t>(graph->partition_num);
                mt->shuffled_messages = m->al_alloc<vertex_id_t>(mt->origin_message_end - mt->origin_message_begin);
                mt->shuffled_states = is_node2vec ? m->al_alloc<vertex_id_t>(mt->origin_message_end - mt->origin_message_begin) : nullptr;
                mt->partition_ids = partition_ids;
                m->align();
            }
        }
        LOG(WARNING) << block_mid_str() << "Initialize MessageManager in " << timer.duration() << " seconds";
    }

    void shuffle(vertex_id_t *messages, walker_state_t *states, walker_id_t active_message_num) {
        Timer timer;

        CHECK(mtasks[mtcfg.socket_num - 1][mtcfg.socket_thread_num() - 1]->origin_message_end >= active_message_num) << mtasks[mtcfg.socket_num - 1][mtcfg.socket_thread_num() - 1]->origin_message_end << " " << active_message_num;
        for (int s_i = 0; s_i < mtcfg.socket_num; s_i++) {
            for (int t_i = mtcfg.socket_thread_num() - 1; t_i >= 0; t_i--) {
                auto mt = mtasks[s_i][t_i];
                if (mt->origin_message_end > active_message_num) {
                    mt->origin_message_end = active_message_num;
                    mt->origin_message_begin = std::min(mt->origin_message_begin, active_message_num);
                } else {
                    break;
                }
            }
        }

        // #if PROFILE_IF_NORMAL
        double shuffle_lv0_phase0_time = 0;
        double thread_time = 0;

        // lv0 message shuffle
        #pragma omp parallel for reduction(+: shuffle_lv0_phase0_time, thread_time)
        for (int t_i = 0; t_i < mtcfg.thread_num; t_i++) {
            int socket = mtcfg.socket_id(t_i);
            int thread_offset = mtcfg.socket_offset(t_i);
            auto mt = mtasks[socket][thread_offset];
            Timer thread_timer;
            mt->prepare(messages);
            shuffle_lv0_phase0_time += thread_timer.duration();
            mt->shuffle(messages, states);
            thread_time += thread_timer.duration();
        }
        shuffle_lv0_phase0_time /= mtcfg.thread_num;


        #if PROFILE_IF_DETAIL
        LOG(INFO) << "\tt1 (generate and shuffle messages): " << get_step_cost(timer.duration(), active_message_num, mtcfg.thread_num) << " ns/step";
        LOG(INFO) << "\t\tshuffle lv0 phase0: " << get_step_cost(shuffle_lv0_phase0_time, active_message_num, mtcfg.thread_num) << " ns/step";
        #endif

        #if PROFILE_IF_BRIEF
        profiler->sub_step_sync_times["2-SHF"] += timer.duration();
        profiler->sub_step_thread_times["2-SHF"] += thread_time / mtcfg.thread_num;
        #endif
    }

    void update(vertex_id_t* target_messages, walker_id_t walker_num) {
        _unused(walker_num);
        Timer timer;
        double thread_time = 0;
        std::vector<int> lv1_task_progess(mtcfg.socket_num, 0);
        #pragma omp parallel reduction(+: thread_time)
        {
            Timer thread_timer;
            int thread_id = omp_get_thread_num();
            int socket = mtcfg.socket_id(thread_id);
            mtasks[socket][mtcfg.socket_offset(thread_id)]->update(target_messages);
            thread_time += thread_timer.duration();
        }
        #if PROFILE_IF_BRIEF
        profiler->sub_step_sync_times["4-UPD"] += timer.duration();
        profiler->sub_step_thread_times["4-UPD"] += thread_time / mtcfg.thread_num;
        #endif

        #if PROFILE_IF_DETAIL
        LOG(INFO) << "\tt3 (update walkers): " << get_step_cost(timer.duration(), walker_num, mtcfg.thread_num) << " ns/step";
        #endif
    }
};
