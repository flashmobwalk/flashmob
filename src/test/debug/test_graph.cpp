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
#include "../../core/partition.hpp"
#include "../test.hpp"
#include "test_graph.hpp"

void test_edges(GraphMocker* graph, std::vector<Edge> &_std_edges, bool as_undirected)
{
    graph->check_edge_consistency();

    std::vector<Edge> std_edges;
    for (auto e : _std_edges) {
        std_edges.push_back(e);
        if (as_undirected) {
            e.transpose();
            std_edges.push_back(e);
        }
    }

    std::vector<Edge> graph_edges;
    graph->get_edges_with_name(graph_edges);

    compare_edges(std_edges, graph_edges);
}

void test_partitions(Graph* graph, int socket_num)
{
    ASSERT_EQ(graph->partition_begin[0], 0u);
    for (int p_i = 1; p_i < graph->partition_num; p_i++) {
        ASSERT_EQ(graph->partition_begin[p_i], graph->partition_end[p_i - 1]);
    }
    ASSERT_EQ(graph->partition_end.back(), graph->v_num);
    for (vertex_id_t v_i = 0; v_i < graph->v_num; v_i++) {
        int p = graph->get_vertex_partition_id(v_i); 
        ASSERT_LE(graph->partition_begin[p], v_i);
        ASSERT_LT(v_i, graph->partition_end[p]);
    }

    ASSERT_EQ(std::accumulate(graph->socket_partition_nums.get(), graph->socket_partition_nums.get() + socket_num, 0), graph->partition_num);
    for (int s_i = 0; s_i < socket_num; s_i++) {
        for (int p_i = 0; p_i < graph->socket_partition_nums[s_i]; p_i++) {
            ASSERT_EQ(graph->partition_socket[graph->socket_partitions[s_i][p_i]], s_i);
            if (p_i > 0) {
                // Ensure the socket partitions are monotonically increasing.
                // If the sum of socket_partition_nums are correct, this also
                // indicate that every partition has appeared exactly once.
                ASSERT_GT(graph->socket_partitions[s_i][p_i], graph->socket_partitions[s_i][p_i - 1]);
            }
        }
    }
}

void test_load_graph(std::vector<Edge> &_std_edges, GraphFormat graph_format, bool as_undirected, MultiThreadConfig mtcfg)
{
    uint64_t mem_quota = 0; // mem_quota is not really used when UNIT_TEST is defined
    auto walker_num_func = [] (vertex_id_t vertex_num, edge_id_t edge_num) {
        uint64_t walker_num;
        if (rand() % 2 == 0) {
            walker_num = (edge_num + rand() % edge_num) * 3;
        } else {
            walker_num = vertex_num + rand() % vertex_num;
        }
        return walker_num;
    };
    GraphMocker graph(mtcfg);
    make_graph(test_graph_path, graph_format, as_undirected, walker_num_func, rand() % 80 + 10, mtcfg, mem_quota, false, graph);

    test_edges(&graph, _std_edges, as_undirected);

    test_partitions(&graph, mtcfg.socket_num);
}

void test_task(GraphFormat graph_format, bool as_undirected, MultiThreadConfig mtcfg) {
    edge_id_t e_nums_arr[] = {3, 9, 64, 128, 1234, 6553};
    std::vector<edge_id_t> e_nums(e_nums_arr, e_nums_arr + 6);
    for (auto &e_num : e_nums_arr)
    {
        std::vector<Edge> edges;
        vertex_id_t v_num = std::min((unsigned)(e_num / 2), (unsigned)(100 + rand() % 100));
        gen_graph(v_num, e_num, edges);
        if (graph_format == BinaryGraphFormat) {
            write_binary_graph(test_graph_path, edges);
        } else {
            write_text_graph(test_graph_path, edges);
        }
        test_load_graph(edges, graph_format, as_undirected, mtcfg);
    }
    rm_test_graph_file();
    if (graph_format == BinaryGraphFormat) {
        rm_test_graph_info_file();
    }
}

TEST(BinaryGraph, SingeThreadDirected)
{
    SINGLE_THREAD_TEST(test_task(BinaryGraphFormat, false, mtcfg));
}

TEST(BinaryGraph, SingeThreadUndirected)
{
    SINGLE_THREAD_TEST(test_task(BinaryGraphFormat, true, mtcfg));
}

TEST(BinaryGraph, MultiThreadDirected)
{
    MULTI_THREAD_TEST(test_task(BinaryGraphFormat, false, mtcfg));
}

TEST(BinaryGraph, MultiThreadUndirected)
{
    MULTI_THREAD_TEST(test_task(BinaryGraphFormat, true, mtcfg));
}

TEST(BinaryGraph, NUMADirected)
{
    NUMA_TEST(test_task(BinaryGraphFormat, false, mtcfg));
}

TEST(BinaryGraph, NUMAUndirected)
{
    NUMA_TEST(test_task(BinaryGraphFormat, true, mtcfg));
}

TEST(TextGraph, SingeThreadDirected)
{
    SINGLE_THREAD_TEST(test_task(TextGraphFormat, false, mtcfg));
}

TEST(TextGraph, SingeThreadUndirected)
{
    SINGLE_THREAD_TEST(test_task(TextGraphFormat, true, mtcfg));
}

TEST(TextGraph, MultiThreadDirected)
{
    MULTI_THREAD_TEST(test_task(TextGraphFormat, false, mtcfg));
}

TEST(TextGraph, MultiThreadUndirected)
{
    MULTI_THREAD_TEST(test_task(TextGraphFormat, true, mtcfg));
}

TEST(TextGraph, NUMADirected)
{
    NUMA_TEST(test_task(TextGraphFormat, false, mtcfg));
}

TEST(TextGraph, NUMAUndirected)
{
    NUMA_TEST(test_task(TextGraphFormat, true, mtcfg));
}

GTEST_API_ int main(int argc, char *argv[])
{
    init_glog(argv, google::FATAL);

    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
