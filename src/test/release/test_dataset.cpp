#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <vector>
#include <utility>
#include <map>
#include <set>
#include <type_traits>

#include <gtest/gtest.h>

#include "io.hpp"
#include "log.hpp"
#include "option.hpp"
#include "../../core/partition.hpp"
#include "../../core/solver.hpp"
#include "../test.hpp"

struct EdgeCounter {
    vertex_id_t dst;
    uint32_t weight;
    uint32_t counter;
    bool operator < (const EdgeCounter &b) const {
        return this->dst < b.dst;
    }
};

struct EdgeCounterList {
    EdgeCounter *begin;
    EdgeCounter *end;
};

struct ProbDistMetric {
    double avg;
    double mean;
    double p99;
};

ProbDistMetric cal_prob_dist_metric(double *probs, uint32_t n) {
    ProbDistMetric result;
    std::sort(probs, probs + n);
    double sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        sum += probs[i];
    }
    result.avg = sum / n;
    result.mean = probs[n / 2];
    result.p99 = probs[(uint32_t)(n * 0.99)];
    return result;
}

GraphOptionParser opt;

void validate_1st_order_prob_distribution(Graph &graph, FMobSolver &solver, int walk_len) {
    Timer timer;
    auto v_num = graph.v_num;
    auto e_num = graph.e_num;

    vertex_id_t *walks = solver.alloc_output_array();

    MemoryPool mpool(opt.mtcfg);
    EdgeCounterList* eclists = mpool.alloc<EdgeCounterList>(v_num, MemoryInterleaved);
    EdgeCounter* ecounters = mpool.alloc<EdgeCounter>(e_num, MemoryInterleaved);

    edge_id_t ecounters_p = 0;
    for (vertex_id_t v_i = 0; v_i < v_num; v_i++) {
        eclists[v_i].begin = ecounters + ecounters_p;
        ecounters_p += graph.adjlists[0][v_i].degree;
    }

    #pragma omp parallel for
    for (vertex_id_t v_i = 0; v_i < v_num; v_i++) {
        vertex_id_t degree = graph.adjlists[0][v_i].degree;
        for (vertex_id_t d_i = 0; d_i < degree; d_i++) {
            eclists[v_i].begin[d_i].dst = graph.adjlists[0][v_i].begin[d_i].neighbor;
            eclists[v_i].begin[d_i].weight = 1;
            eclists[v_i].begin[d_i].counter = 0;
        }
        std::sort(eclists[v_i].begin, eclists[v_i].begin + degree);
        vertex_id_t ec_p = 0;
        for (vertex_id_t d_i = 0; d_i < degree; d_i++) {
            if (ec_p != 0 && eclists[v_i].begin[ec_p - 1].dst == eclists[v_i].begin[d_i].dst) {
                eclists[v_i].begin[ec_p - 1].weight ++;
            } else {
                eclists[v_i].begin[ec_p] = eclists[v_i].begin[d_i];
                ec_p++;
            }
        }
        eclists[v_i].end = eclists[v_i].begin + ec_p;
    }
    // Bhattacharyya distance
    double* vertex_bd = mpool.alloc_new<double>(v_num, MemoryInterleaved);
    // Total variance distance
    double* vertex_tvd = mpool.alloc_new<double>(v_num, MemoryInterleaved);

    std::cout << "Initiate validator in " << timer.duration() << " seconds" << std::endl;
    timer.restart();

    uint64_t processed_step_num = 0;
    bool converged = false;
    while (solver.has_next_walk() && !converged) {
        walker_id_t epoch_walker_num;
        solver.walk(walks, epoch_walker_num);

        processed_step_num += (uint64_t) epoch_walker_num * walk_len;
        std::cout << "Checking results after " << (uint64_t) epoch_walker_num * walk_len << " steps (" << processed_step_num << " in total)" << std::endl;

		//check if sequences are legal
        #pragma omp parallel for 
		for (walker_id_t w_i = 0; w_i < epoch_walker_num; w_i++) {
			vertex_id_t *path = walks + (uint64_t) w_i * walk_len;
			for (int v_i = 0; v_i + 1 < walk_len; v_i++) {
                vertex_id_t src = path[v_i];
                vertex_id_t dst = path[v_i + 1];
                EdgeCounter temp;
                temp.dst = dst;
                EXPECT_TRUE(std::binary_search(eclists[src].begin, eclists[src].end, temp));
                EdgeCounter* ecounter = std::lower_bound(eclists[src].begin, eclists[src].end, temp);
                __sync_fetch_and_add(&(ecounter->counter), 1);
			}
		}

        #pragma omp parallel for
        for (vertex_id_t v_i = 0; v_i < v_num; v_i++) {
            uint64_t sum_0 = 0;
            uint64_t sum_1 = 0;
            vertex_id_t n = eclists[v_i].end - eclists[v_i].begin;
            for (vertex_id_t e_i = 0; e_i < n; e_i++) {
                sum_0 += eclists[v_i].begin[e_i].weight;
                sum_1 += eclists[v_i].begin[e_i].counter;
            }
            double bd_distance;
            if (sum_0 > 0 && sum_1 > 0) {
                double bd_coefficient = 0;
                for (vertex_id_t e_i = 0; e_i < n; e_i++) {
                    double a0 = (double) eclists[v_i].begin[e_i].weight / sum_0;
                    double a1 = (double) eclists[v_i].begin[e_i].counter / sum_1;
                    bd_coefficient += std::sqrt(a0 * a1);
                }
                bd_distance = log(bd_coefficient) * -1.0;
            } else {
                bd_distance = 10; // set max value
            }
            vertex_bd[v_i] = bd_distance;
        }
        ProbDistMetric bd_result = cal_prob_dist_metric(vertex_bd, v_num);
        printf("\tBhattacharyya distance: avg %.4lf, mean %.4lf, p99 %.4lf\n", bd_result.avg, bd_result.mean, bd_result.p99);

        #pragma omp parallel for
        for (vertex_id_t v_i = 0; v_i < v_num; v_i++) {
            uint64_t sum_0 = 0;
            uint64_t sum_1 = 0;
            vertex_id_t n = eclists[v_i].end - eclists[v_i].begin;
            for (vertex_id_t e_i = 0; e_i < n; e_i++) {
                sum_0 += eclists[v_i].begin[e_i].weight;
                sum_1 += eclists[v_i].begin[e_i].counter;
            }
            double tvd_distance = 0;
            if (sum_0 > 0 && sum_1 > 0) {
                for (vertex_id_t e_i = 0; e_i < n; e_i++) {
                    double a0 = (double) eclists[v_i].begin[e_i].weight / sum_0;
                    double a1 = (double) eclists[v_i].begin[e_i].counter / sum_1;
                    tvd_distance = std::max(tvd_distance, std::max(a0 - a1, a1 - a0));
                }
            } else {
                tvd_distance = 1;
            }
            vertex_tvd[v_i] = tvd_distance;
        }
        ProbDistMetric tvd_result = cal_prob_dist_metric(vertex_tvd, v_num);
        printf("\tTotal variation distance: avg %.4lf, mean %.4lf, p99 %.4lf\n", tvd_result.avg, tvd_result.mean, tvd_result.p99);

        if (bd_result.avg < 0.005 && \
            bd_result.mean < 0.005 && \
            bd_result.p99 < 0.015 && \
            tvd_result.avg < 0.01 && \
            tvd_result.mean < 0.01 && \
            tvd_result.p99 < 0.03 \
        ) {
            converged = true;
        }
    }

    ProbDistMetric bd_result = cal_prob_dist_metric(vertex_bd, v_num);
    EXPECT_TRUE(bd_result.avg < 0.005);
    EXPECT_TRUE(bd_result.mean < 0.005);
    EXPECT_TRUE(bd_result.p99 < 0.015);

    ProbDistMetric tvd_result = cal_prob_dist_metric(vertex_tvd, v_num);
    EXPECT_TRUE(tvd_result.avg < 0.01);
    EXPECT_TRUE(tvd_result.mean < 0.01);
    EXPECT_TRUE(tvd_result.p99 < 0.03);

    solver.dealloc_output_array(walks);
}

void validate_prob_distribution(bool is_node2vec) {
    Timer timer;
    int walk_len = 80;
    auto walker_num_func = [] (vertex_id_t vertex_num, edge_id_t edge_num) {
        uint64_t walker_num = (uint64_t) (vertex_num + edge_num) * 100;
        return walker_num;
    };
    Graph graph(opt.mtcfg);
    make_graph(opt.graph_path.c_str(), opt.graph_format, true, walker_num_func, walk_len, opt.mtcfg, opt.mem_quota, false, graph);
    vertex_id_t v_num = graph.v_num;
    edge_id_t e_num = graph.e_num;

    size_t validator_size = sizeof(double) * v_num * 2 + sizeof(EdgeCounterList) * v_num + sizeof(EdgeCounter) * e_num;
    FMobSolver solver(&graph, opt.mtcfg);
    if (is_node2vec) {
        solver.set_node2vec(1.0, 1.0);
    }
    CHECK(opt.mem_quota > validator_size) << "Not enough memory";
    solver.prepare(walker_num_func(v_num, e_num), walk_len, opt.mem_quota - validator_size);
    printf("Initiate Graph and Solver in %.3lf seconds\n", timer.duration());

    validate_1st_order_prob_distribution(graph, solver, walk_len);
    std::cout << "Validate in " << timer.duration() << " seconds" << std::endl;
}

TEST(RealGraph, DeepWalk)
{
    validate_prob_distribution(false);
}

TEST(RealGraph, node2vec)
{
    validate_prob_distribution(true);
}

GTEST_API_ int main(int argc, char *argv[])
{
    init_glog(argv, google::FATAL);
    ::testing::InitGoogleTest(&argc, argv);

    opt.parse(argc, argv);
    init_concurrency(opt.mtcfg);

    int result = RUN_ALL_TESTS();
    return result;
}
