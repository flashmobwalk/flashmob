#pragma once

#include <math.h>
#include <sstream>
#include <map>
#include <set>

#include "timer.hpp"
#include "io.hpp"
#include "util.hpp"
#include "graph.hpp"
#include "sampler.hpp"

struct SampleEstimation {
    SamplerClass sampler_class;
    double step_time;
};

typedef std::map<vertex_id_t, std::map<vertex_id_t, std::vector<SampleEstimation> > > MiniBMKCatMap;

/**
 * Manages mini benchmark results.
 *
 * It will first check ./.fmob/ directory to load previous results.
 * Only if a data point has not been stored before, will it be profiled.
 * Any new result will be store to files for future reference.
 *
 */
class MiniBMKCatManager {
public:
    struct MiniBMKItem {
        vertex_id_t partition_bits;
        vertex_id_t degree;
        SamplerClass sampler_class;
        double step_time;
        bool friend operator < (const MiniBMKItem& a, const MiniBMKItem& b) {
            if (a.partition_bits != b.partition_bits) {
                return a.partition_bits < b.partition_bits;
            }
            if (a.degree != b.degree) {
                return a.degree < b.degree;
            }
            return a.sampler_class < b.sampler_class;
        }

        MiniBMKItem(vertex_id_t _partition_bits, vertex_id_t _degree, SamplerClass _sampler_class, double _step_time = 0) {
            partition_bits = _partition_bits;
            degree = _degree;
            sampler_class = _sampler_class;
            step_time = _step_time;
        }

        MiniBMKItem() {}
    };
private:
    std::string cfg_name;
    std::string cfg_dir;
    std::string cfg_file;
    std::set<MiniBMKItem> cat_set;
    int new_item_num;
public:
    MiniBMKCatManager (double walker_per_edge, MultiThreadConfig mtcfg) {
        cfg_dir = FMobDir;
        std::string cmd = std::string("mkdir -p ") + cfg_dir;
        CHECK(0 == system(cmd.c_str()));

        // log(walker_per_edge, 1.5) with precision of 0
        double wpe_log = log(walker_per_edge) / log(1.5);
        std::stringstream cfg_name_ss;
        cfg_name_ss << std::fixed << std::setprecision(0) << wpe_log << "_" << mtcfg.socket_num << "_" << mtcfg.thread_num << ".txt";
        cfg_name = cfg_name_ss.str();
        cfg_file = cfg_dir + "/" + cfg_name;

        LOG(WARNING) << block_mid_str(1) << "Mini-benchmark file: " << cfg_file;

        FILE* f = fopen(cfg_file.c_str(), "r");
        if (f != NULL) {
            MiniBMKItem item;
            uint32_t sampler_class;
            while (4 == fscanf(f, "%u %u %u %lf", &item.partition_bits, &item.degree, &sampler_class, &item.step_time)) {
                item.sampler_class = static_cast<SamplerClass>(sampler_class);
                cat_set.insert(item);
            }
            fclose(f);
        }
        new_item_num = 0;
    }

    void get_catalogue(MiniBMKCatMap *cat_map) {
        for (auto &item : cat_set) {
            SampleEstimation est;
            est.sampler_class = item.sampler_class;
            est.step_time = item.step_time;
            (*cat_map)[item.partition_bits][item.degree].push_back(est);
        }
    }

    bool has_item(MiniBMKItem item) {
        return cat_set.find(item) != cat_set.end();
    }

    void add_item(MiniBMKItem item) {
        CHECK(cat_set.find(item) == cat_set.end());
        cat_set.insert(item);
        new_item_num ++;
    }

    void save_catalogue() {
        LOG(WARNING) << block_mid_str(1) << "New mini benchmarks: " << new_item_num;
        if (new_item_num != 0) {
            FILE* f = fopen(cfg_file.c_str(), "w");
            for (auto &item : cat_set) {
                fprintf(f, "%u %u %u %lf\n", item.partition_bits, item.degree, item.sampler_class, item.step_time);
            }
            fclose(f);
        }
    }
};

/**
 * A mocker of walk_message in WalkManager.
 *
 * Mini benchmark only tests partitions where #vertices, #edges, #walkers fall into
 * certan ranges. The performance of partitions with too many/few vertices/edgtes/walkers
 * could be inferred from the performance of other partitions.
 *
 */
template<typename sampler_t>
void walk_message_mock(sampler_t *sampler, vertex_id_t *message_begin, vertex_id_t *message_end, vertex_id_t bitmask, default_rand_t *rd) {
    for (vertex_id_t *msg = message_begin; msg < message_end; msg ++) {
        *msg = sampler->sample((*msg) & bitmask, rd);
    }
}

void mini_benchmark(
    double walker_per_edge,
    vertex_id_t max_degree,
    vertex_id_t min_partition_vertex_bit,
    vertex_id_t max_partition_vertex_bit,
    MultiThreadConfig mtcfg,
    std::map<vertex_id_t, std::map<vertex_id_t, std::vector<SampleEstimation> > > &results
) {
    LOG(WARNING) << block_begin_str(1) << "Mini benchmarks";
    struct BmkTask {
        vertex_id_t ptn_bits;
        SamplerClass sclass;
    };
    Timer benchmark_timer;
    MiniBMKCatManager cat_manager(walker_per_edge, mtcfg);
    const vertex_id_t internal_max_pt_bit = std::min(max_partition_vertex_bit, std::max(20u, min_partition_vertex_bit));
    const edge_id_t thread_edge_num = 1ull << 24;
    const vertex_id_t max_thread_vertex_num = 1 << internal_max_pt_bit;
    const uint64_t max_thread_walker_num = thread_edge_num * walker_per_edge;
    const vertex_id_t max_value = max_thread_vertex_num;

    std::vector<vertex_id_t> test_degrees;
    for (vertex_id_t d_i = 1; d_i <= max_degree;) {
        test_degrees.push_back(d_i);
        d_i = std::max(d_i + 1, (vertex_id_t) (d_i * 1.05));
    }

    std::map<vertex_id_t, std::vector<BmkTask> > bmk_tasks;
    for (auto degree : test_degrees) {
        for (vertex_id_t partition_bits = min_partition_vertex_bit; partition_bits <= internal_max_pt_bit; partition_bits++) {
            vertex_id_t partition_vertex_num = 1 << partition_bits;
            if ((uint64_t) partition_vertex_num * degree > thread_edge_num \
                || (uint64_t) partition_vertex_num * degree * walker_per_edge > max_thread_walker_num) {
                continue;
            }
            uint64_t partition_walker_num = (uint64_t) partition_vertex_num * degree * walker_per_edge;
            if (partition_walker_num < 1) {
                continue;
            }
            BmkTask task;
            task.ptn_bits = partition_bits;
            if (!cat_manager.has_item(MiniBMKCatManager::MiniBMKItem(partition_bits, degree, ClassUniformDegreeDirectSampler))) {
                task.sclass = ClassUniformDegreeDirectSampler;
                bmk_tasks[degree].push_back(task);
            }
            if (degree > 4 && !cat_manager.has_item(MiniBMKCatManager::MiniBMKItem(partition_bits, degree, ClassExclusiveBufferSampler))) {
                task.sclass = ClassExclusiveBufferSampler;
                bmk_tasks[degree].push_back(task);
            }
        }
    }

    // Only allocate resources for mini benchmarks when there are new tests to run
    if (bmk_tasks.size() > 0) {
        MemoryPool mpool(mtcfg);

        default_rand_t *rands[mtcfg.thread_num];
        for (int t_i = 0; t_i < mtcfg.thread_num; t_i++) {
            rands[t_i] = mpool.alloc_new<default_rand_t>(1, mtcfg.socket_id(t_i));
        }

        AdjList *adjlists[mtcfg.thread_num];
        for (int t_i = 0; t_i < mtcfg.thread_num; t_i++) {
            adjlists[t_i] = mpool.alloc_new<AdjList>(max_thread_vertex_num, mtcfg.socket_id(t_i));
        }

        AdjUnit *adjunits[mtcfg.thread_num];
        for (int t_i = 0; t_i < mtcfg.thread_num; t_i++) {
            adjunits[t_i] = mpool.alloc_new<AdjUnit>(thread_edge_num, mtcfg.socket_id(t_i));
        }

        vertex_id_t *walkers[mtcfg.thread_num];
        for (int t_i = 0; t_i < mtcfg.thread_num; t_i++) {
            walkers[t_i] = mpool.alloc_new<vertex_id_t>(max_thread_walker_num, mtcfg.socket_id(t_i));
        }

        #pragma omp parallel
        {
            int thread = omp_get_thread_num();
            auto *rd = rands[thread];
            for (edge_id_t e_i = 0; e_i < thread_edge_num; e_i++) {
                adjunits[thread][e_i].neighbor = rd->gen(max_value);
            }
            for (walker_id_t w_i = 0; w_i < max_thread_walker_num; w_i++) {
                walkers[thread][w_i] = rd->gen(max_value);
            }
        }

        vertex_id_t progress = 0;
        uint64_t rand_sum = 0;
        volatile int finished_thread_num = 0;
        std::mutex cat_manager_lock;
        #pragma omp parallel reduction (+: rand_sum)
        {
            Timer thread_timer;
            int thread = omp_get_thread_num();
            int socket = mtcfg.socket_id(thread);
            auto *rd = rands[thread];

            vertex_id_t progress_i;
            while((progress_i = __sync_fetch_and_add(&progress, 1)) < test_degrees.size()) {
                vertex_id_t degree = test_degrees[progress_i];
                if (bmk_tasks.find(degree) == bmk_tasks.end()) {
                    continue;
                }

                MemoryPool local_mpool(mtcfg);
                for (vertex_id_t v_i = 0; v_i < max_thread_vertex_num; v_i++) {
                    adjlists[thread][v_i].begin = adjunits[thread] + v_i * degree;
                    adjlists[thread][v_i].degree = degree;
                }

                for (auto &task : bmk_tasks[degree]) {
                    vertex_id_t partition_vertex_num = 1 << task.ptn_bits;
                    uint64_t partition_walker_num = (uint64_t) partition_vertex_num * degree * walker_per_edge;
                    if (task.sclass == ClassUniformDegreeDirectSampler) {
                        UniformDegreeDirectSampler sampler;
                        sampler.init(0, partition_vertex_num, adjlists[thread]);

                        Timer timer;
                        uint64_t work = 0;
                        double work_time = 0;
                        uint32_t iter_num = std::max(4ul, (1ul << 20) / partition_walker_num);
                        for (vertex_id_t iter_i = 0; iter_i < iter_num; iter_i++) {
                            sampler.reset(0, partition_vertex_num, adjlists[thread]);
                            timer.restart();
                            walk_message_mock(&sampler, walkers[thread], walkers[thread] + partition_walker_num, partition_vertex_num - 1, rd);
                            work_time += timer.duration();
                            work += partition_walker_num;
                        }
                        double time = get_step_cost(work_time, work, 1);
                        cat_manager_lock.lock();
                        cat_manager.add_item(MiniBMKCatManager::MiniBMKItem(task.ptn_bits, degree, ClassUniformDegreeDirectSampler, time));
                        cat_manager_lock.unlock();
                    } else if (task.sclass == ClassExclusiveBufferSampler) {
                        ExclusiveBufferSampler sampler;
                        sampler.init(0, partition_vertex_num, adjlists[thread], &local_mpool, socket);

                        uint64_t work = 0;
                        double work_time = 0;
                        Timer timer;
                        vertex_id_t iter_num = std::max(4ul, std::max(1ul << 20, 4ul *  sampler.buffer_unit_num) / partition_walker_num);
                        for (vertex_id_t iter_i = 0; iter_i < iter_num; iter_i++) {
                            sampler.reset(0, partition_vertex_num, adjlists[thread]);
                            timer.restart();
                            walk_message_mock(&sampler, walkers[thread], walkers[thread] + partition_walker_num, partition_vertex_num - 1, rd);
                            work_time += timer.duration();
                            work += partition_walker_num;
                        }
                        double time = get_step_cost(work_time, work, 1);
                        cat_manager_lock.lock();
                        cat_manager.add_item(MiniBMKCatManager::MiniBMKItem(task.ptn_bits, degree, ClassExclusiveBufferSampler, time));
                        cat_manager_lock.unlock();
                    }
                }
            }
            __sync_fetch_and_add(&finished_thread_num, 1);
            while (finished_thread_num != mtcfg.thread_num) {
                for (int i = 0; i < 1024; i++) {
                    rand_sum += adjunits[thread][rd->gen(thread_edge_num)].neighbor;
                }
            }
            //printf("Thread %d: %.3lf sec\n", thread, thread_timer.duration());
        }
        // To avoid rand_sum been optimized by compiler.
        if ((rand_sum & 0xFFFFFF) == 0) {
            LOG(INFO) << "Lucky";
        }
    }

    cat_manager.save_catalogue();
    cat_manager.get_catalogue(&results);

    for (vertex_id_t pt_bit = internal_max_pt_bit + 1; pt_bit <= max_partition_vertex_bit; pt_bit++) {
        results[pt_bit] = results[internal_max_pt_bit];
    }

    LOG(WARNING) << block_end_str(1) << "Mini benchmarks in " << benchmark_timer.duration() << " sec";
    /*
    for (auto iter : results) {
        printf("partition_bits %u\n", iter.first);
        for (auto &iter_deg: iter.second) {
            printf("\tdegree %u: ", iter_deg.first);
            for (auto res : iter_deg.second) {
                if (res.sampler_class == ClassExclusiveBufferSampler) {
                    printf(" (PS, %.3lf ns),", res.step_time);
                }
                if (res.sampler_class == ClassUniformDegreeDirectSampler) {
                    printf(" (DS, %.3lf ns),", res.step_time);
                }
            }
            printf("\n");
        }
    }
    exit(0);
    */
}
