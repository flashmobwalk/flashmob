/**
 * Code in this file are adapted from:
 * https://github.com/KnightKingWalk/KnightKing/blob/master/src/tests/test_node2vec.cpp
 *
 */

/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ke Yang, Tsinghua University 
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>

#include <vector>
#include <utility>
#include <map>
#include <set>

#include <gtest/gtest.h>

#include "type.hpp"
#include "log.hpp"
#include "test_walk.hpp"

void get_node2vec_trans_matrix(vertex_id_t v_num, Edge *edges, edge_id_t e_num, double p, double q,  std::vector<std::vector<double> > &trans_mat)
{
    std::vector<std::vector<Edge> > graph(v_num);
    for (edge_id_t e_i = 0; e_i < e_num; e_i++)
    {
        graph[edges[e_i].src].push_back(edges[e_i]);
    }
    for (vertex_id_t v_i = 0; v_i < v_num; v_i++)
    {
        std::sort(graph[v_i].begin(), graph[v_i].end(), [](const Edge a, const Edge b){return a.dst < b.dst;});
    }
    for (edge_id_t e_i = 0; e_i < e_num; e_i++)
    {
        vertex_id_t src = edges[e_i].src;
        vertex_id_t dst = edges[e_i].dst;
        assert(src != dst);
        //must be undirected graph
        assert(graph[dst].size() != 0);
        for (auto e : graph[dst])
        {
            if (e.dst == src)
            {
                trans_mat[e_i][e.dst] += 1 / p; // * get_edge_trans_weight(e);
            } else if (std::binary_search(graph[src].begin(), graph[src].end(), e, [](const Edge a, const Edge b){return a.dst < b.dst;}))
            {
                trans_mat[e_i][e.dst] += 1; // * get_edge_trans_weight(e);
            } else
            {
                trans_mat[e_i][e.dst] += 1 / q; // * get_edge_trans_weight(e);
            }
        }
    }
    mat_normalization(trans_mat);
}

void check_node2vec_random_walk(vertex_id_t v_num, Edge *edges, edge_id_t e_num, double p, double q, vertex_id_t *walks, walker_id_t walker_num, int walk_len)
{
    std::vector<std::vector<double> > trans_mat(e_num);
    for (auto &vec : trans_mat)
    {
        vec.resize(v_num, 0.0);
    }
    get_node2vec_trans_matrix(v_num, edges, e_num, p, q, trans_mat);

    //check if sequences are legal
    std::vector<vertex_id_t> out_degree(v_num, 0);
    std::vector<std::vector<bool> > adj_mat(v_num);
    for (auto &vec : adj_mat)
    {
        vec.resize(v_num, false);
    }
    for (edge_id_t e_i = 0; e_i < e_num; e_i++)
    {
        adj_mat[edges[e_i].src][edges[e_i].dst] = true;
        out_degree[edges[e_i].src]++;
    }
    for (walker_id_t w_i = 0; w_i < walker_num; w_i++)
    {
        /*
        if (out_degree[s[0]] == 0)
        {
            for (auto v : s)
            {
                ASSERT_EQ(v, s[0]);
            }
        } else
        */
        auto *path = walks + w_i * walk_len;
        for (int p_i = 0; p_i + 1 < walk_len; p_i++)
        {
            if (adj_mat[path[p_i]][path[p_i + 1]] == false)
            {
                printf("fault %u %u\n", path[p_i], path[p_i + 1]);
            }
            ASSERT_TRUE(adj_mat[path[p_i]][path[p_i + 1]]);
        }
    }

    std::map<std::pair<vertex_id_t, vertex_id_t>, edge_id_t> dict;
    for (edge_id_t e_i = 0; e_i < e_num; e_i++)
    {
        // printf("%u %u\n", edges[e_i].src, edges[e_i].dst);
        std::pair<vertex_id_t, vertex_id_t> key = std::pair<vertex_id_t, vertex_id_t>(edges[e_i].src, edges[e_i].dst);
        assert(dict.find(key) == dict.end());
        dict[key] = e_i;
    }

    std::vector<std::vector<double> > real_trans_mat(e_num);
    for (auto &vec : real_trans_mat)
    {
        vec.resize(v_num, 0.0);
    }
    for (walker_id_t w_i = 0; w_i < walker_num; w_i++)
    {
        auto *path = walks + w_i * walk_len;
        for (int p_i = 0; p_i + 2 < walk_len; p_i++)
        {
            real_trans_mat[dict[std::pair<vertex_id_t, vertex_id_t>(path[p_i], path[p_i + 1])]][path[p_i + 2]] += 1;
        }
    }
    mat_normalization(real_trans_mat);
    cmp_trans_matrix(real_trans_mat, trans_mat, 10.0);
}