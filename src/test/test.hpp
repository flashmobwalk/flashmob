#pragma once

#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <vector>
#include <utility>
#include <map>
#include <set>
#include <type_traits>

#include <gtest/gtest.h>

#include "type.hpp"
#include "random.hpp"
#include "numa_helper.hpp"
#include "env.hpp"

// A temporary file will be generated during the tests and will be
// removed afterwards.
const char *test_graph_path = "./.flashmobtest.txt";

void rm_test_graph_file()
{
    std::remove(test_graph_path);
}

void rm_test_graph_info_file()
{
    std::remove(get_info_graph_path(test_graph_path).c_str());
}

// Randomly generate a graph
void gen_graph(vertex_id_t vertex_num, edge_id_t edge_num, std::vector<Edge> &edges) {
    assert(vertex_num <= edge_num);
    MTRandGen rd;
    edges.clear();
    for (edge_id_t e_i = 0; e_i < edge_num; e_i++) {
        vertex_id_t src, dst;
        if (e_i < vertex_num) {
            // to ensure each vertex appears at least once;
            src = e_i;
        } else {
            src = rd.gen(vertex_num);
        }
        dst = rd.gen(vertex_num);
        edges.push_back(Edge(src, dst));
    }
}

// Randomly generate an undirected graph. No duplicate edge will be produced.
void gen_undirected_graph(vertex_id_t vertex_num, edge_id_t edge_num, std::vector<Edge> &edges) {
    assert(vertex_num <= edge_num * 2 && edge_num % 2 == 0);
    assert(vertex_num * (vertex_num - 1) >= edge_num);
    MTRandGen rd;
    edges.clear();
    std::set<std::pair<vertex_id_t, vertex_id_t>> edge_set;
    for (vertex_id_t v_i = 0; v_i < vertex_num; v_i++) {
        vertex_id_t src, dst;
        src = v_i;
        do {
            dst = rd.gen(vertex_num);
        } while (src == dst);
        if (edge_set.find(std::pair<vertex_id_t, vertex_id_t>(src, dst)) == edge_set.end()) {
            edge_set.insert(std::pair<vertex_id_t, vertex_id_t>(src, dst));
            edge_set.insert(std::pair<vertex_id_t, vertex_id_t>(dst, src));
            edges.push_back(Edge(src, dst));
            edges.push_back(Edge(dst, src));
        }
    }
    while (edges.size() < edge_num) {
        vertex_id_t src, dst;
        do {
            src = rd.gen(vertex_num);
            dst = rd.gen(vertex_num);
        } while (src == dst || edge_set.find(std::pair<vertex_id_t, vertex_id_t>(src, dst)) != edge_set.end());
        edge_set.insert(std::pair<vertex_id_t, vertex_id_t>(src, dst));
        edge_set.insert(std::pair<vertex_id_t, vertex_id_t>(dst, src));
        edges.push_back(Edge(src, dst));
        edges.push_back(Edge(dst, src));
    }
    /*
    for (auto e: edges) {
        printf("org %u %u\n", e.src, e.dst);
    }
    */
}

// Run tests with different concurrency settings.
#define FOR_CONCURRENCY_TEST(run, thread_lower, thread_upper, socket_lower, socket_upper) \
    int max_threads = get_max_core_num(); \
    int max_sockets = get_max_socket_num(); \
    int threads_per_socket = max_threads / max_sockets; \
    std::vector<int> threads; \
    std::vector<int> sockets; \
    for (int t = 1; t < threads_per_socket; t *= 2) { \
        threads.push_back(t); \
        sockets.push_back(1); \
    } \
    for (int s = 1; s <= max_sockets; s *= 2) { \
        threads.push_back(threads_per_socket * s); \
        sockets.push_back(s); \
    } \
    for (unsigned c_i = 0; c_i < threads.size(); c_i++) { \
        if (thread_lower <= threads[c_i] && threads[c_i] <= thread_upper && socket_lower <= sockets[c_i] && sockets[c_i] <= socket_upper) { \
            MultiThreadConfig mtcfg; \
            mtcfg.thread_num = threads[c_i]; \
            mtcfg.socket_num = sockets[c_i]; \
            mtcfg.l2_cache_size = get_l2_cache_size(); \
            init_concurrency(mtcfg); \
            run; \
        } \
    }


#define SINGLE_THREAD_TEST(run) \
    FOR_CONCURRENCY_TEST(run, 1, 1, 1, 1)

#define MULTI_THREAD_TEST(run) \
    FOR_CONCURRENCY_TEST(run, 2, 128, 1, 1)

#define NUMA_TEST(run) \
    FOR_CONCURRENCY_TEST(run, 2, 128, 2, 128)
