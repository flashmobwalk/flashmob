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
#include "../test.hpp"
#include "test_graph.hpp"
#include "knightking/test_walk.hpp"
#include "knightking/test_node2vec.hpp"
#include "../../core/graph.hpp"
#include "../../core/solver.hpp"

void test_node2vec(real_t p, real_t q, GraphFormat graph_format, MultiThreadConfig mtcfg)
{
    uint64_t mem_quota = 0; // mem_quota is not really used when UNIT_TEST is defined
    unsigned walk_len = 40 + rand() % 40;
    auto walker_num_func = [] (vertex_id_t vertex_num, edge_id_t edge_num) {
        uint64_t walker_num = (edge_num + rand() % edge_num) * 30;
        return walker_num;
    };
    GraphMocker graph(mtcfg);
    make_graph(test_graph_path, graph_format, false, walker_num_func, walk_len, mtcfg, mem_quota, true, graph);
    graph.prepare_neighbor_query();

    FMobSolver* solver = new FMobSolver(&graph, mtcfg);

    solver->set_node2vec(p, q);
    uint64_t walker_num = walker_num_func(graph.v_num, graph.e_num);
    std::vector<vertex_id_t> walks((size_t) walk_len * walker_num);
    solver->prepare(walker_num, walk_len, mem_quota);
    auto* temp_walks = solver->alloc_output_array();
    uint64_t terminated_walker_num = 0;
    while (solver->has_next_walk()) {
        walker_id_t epoch_walker_num;
        solver->walk(temp_walks, epoch_walker_num);
        memcpy(walks.data() + terminated_walker_num * walk_len, temp_walks, epoch_walker_num * sizeof(vertex_id_t) * walk_len);
        terminated_walker_num += epoch_walker_num;
    }
    solver->dealloc_output_array(temp_walks);
    ASSERT_EQ(terminated_walker_num, walker_num);

    // To get edges with respect to the vertex id instead of vertex name
    std::vector<Edge> graph_edges;
    graph.get_edges_with_id(graph_edges);
    check_node2vec_random_walk(graph.v_num, graph_edges.data(), graph_edges.size(), p, q, walks.data(), walker_num, walk_len);

    delete solver;
}

void test_task(MultiThreadConfig mtcfg) {
    edge_id_t e_nums_arr[] = {8, 64, 128, 232, 654, 800};
    for (auto &e_num : e_nums_arr)
    {
        std::vector<Edge> edges;
        vertex_id_t v_num = std::min((unsigned)(e_num / 2), (unsigned)(100 + rand() % ((e_num + 9) / 3)));
        gen_undirected_graph(v_num, e_num, edges);
        write_text_graph(test_graph_path, edges);
        test_node2vec(0.5, 2.0, TextGraphFormat, mtcfg);
        test_node2vec(2.0, 0.5, TextGraphFormat, mtcfg);
        test_node2vec(10, 10, TextGraphFormat, mtcfg);
    }
    rm_test_graph_file();
}

TEST(Node2vec, SingleThread)
{
    SINGLE_THREAD_TEST(test_task(mtcfg));
}

TEST(Node2vec, MultiThread)
{
    MULTI_THREAD_TEST(test_task(mtcfg));
}

TEST(Node2vec, NUMA)
{
    NUMA_TEST(test_task(mtcfg));
}

GTEST_API_ int main(int argc, char *argv[])
{
    init_glog(argv, google::FATAL);

    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
