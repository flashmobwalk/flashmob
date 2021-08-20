#pragma once

#include <stdio.h>
#include <stdlib.h>

#include <vector>

#include <gtest/gtest.h>

#include "../core/graph.hpp"

void compare_edges(std::vector<Edge> &std_edges, std::vector<Edge> &cmp_edges) {
    auto cmp_func = [](const Edge &a, const Edge &b) {
        if (a.src != b.src) {
            return a.src < b.src;
        } else {
            return a.dst < b.dst;
        }
    };
    std::sort(std_edges.begin(), std_edges.end(), cmp_func);
    std::sort(cmp_edges.begin(), cmp_edges.end(), cmp_func);
	ASSERT_EQ(cmp_edges.size(), std_edges.size());
	for (size_t e_i = 0; e_i < std_edges.size(); e_i++) {
        auto &cmp_e = cmp_edges[e_i];
        auto &std_e = std_edges[e_i];
        EXPECT_EQ(cmp_e.src, std_e.src);
        EXPECT_EQ(cmp_e.dst, std_e.dst);
	}
}

class GraphMocker : public Graph {
    void get_edge_set(std::vector<Edge> &edges, int socket_id) {
        for (vertex_id_t v_i = 0; v_i < v_num; v_i++) {
            auto *begin = adjlists[socket_id][v_i].begin;
            auto *end = adjlists[socket_id][v_i].begin + adjlists[socket_id][v_i].degree;
            vertex_id_t src = v_i;
            for (auto *p = begin; p < end; p++) {
                vertex_id_t dst = p->neighbor;
                edges.push_back(Edge(src, dst));
            }
        }
    }
public:
    GraphMocker(MultiThreadConfig _mtcfg) : Graph(_mtcfg) {}

    void check_edge_consistency() {
        std::vector<Edge> edges_0;
        get_edge_set(edges_0, 0);
		for (int s_i = 1; s_i < mtcfg.socket_num; s_i++) {
            std::vector<Edge> edges_x;
            get_edge_set(edges_x, s_i);
            
            compare_edges(edges_0, edges_x);
        }
    }

    void get_edges_with_id(std::vector<Edge> &edges) {
        get_edge_set(edges, 0);
    }

    void get_edges_with_name(std::vector<Edge> &edges) {
        get_edges_with_id(edges);
        for (auto &e : edges) {
            e.src = id2name[e.src];
            e.dst = id2name[e.dst];
        }
    }
};
