#pragma once

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <vector>
#include <unordered_map>
#include <cstddef>
#include <memory>
#include <algorithm>

#include <omp.h>

#include "type.hpp"
#include "util.hpp"
#include "timer.hpp"
#include "log.hpp"
#include "io.hpp"
#include "constants.hpp"
#include "random.hpp"
#include "memory.hpp"
#include "hash.hpp"

struct AdjUnit {
    vertex_id_t neighbor;
};

struct AdjList {
    vertex_id_t degree;
    AdjUnit *begin;
} __attribute__((packed));

/**
 * Brief description of a group used in Graph class.
 *
 * The partitions in the group starts from partition_offset.
 * Each partition has (1 << partition_bits) vertices.
 *
 */
struct GroupHeader {
    vertex_id_t partition_bits;
    vertex_id_t partition_offset;
};

/**
 * Description of a group to help Graph class partition the graph.
 *
 * The vertices in the group are [vertex_begin, vertex_end).
 * There are partition_num partitions in the group, each has
 * (1 << partition_bits) vertices. The estimated total time for
 * all the walkers on vertices within this group to take one 1 step
 * is total_time, the average of which is step_time. Partition_level
 * larger than 0 indicates that the partitions in this group will be
 * further partitioned into sub-partitions. Currently even it's added
 * to the model, on all evaluated graphs only the single level partitioning
 * is selected by DP algorithm. Additional level shuffling needs to be
 * added here.
 *
 */
struct GroupHint {
    vertex_id_t vertex_begin;
    vertex_id_t vertex_end;
    vertex_id_t partition_bits;
    vertex_id_t partition_num;
    double total_time;
    double step_time;
    int partition_level;
};

/**
 * Description of the groups and partitions to help Graph class partition the graph.
 *
 * Each group has (1<< group_bits) vertices, and there are group_num groups in total.
 * The partition_sampler_class has #partitions elements, giving the sampler type
 * suggestions for each partition.
 *
 */
struct GraphHint {
    vertex_id_t group_bits;
    vertex_id_t group_num;
    std::vector<GroupHint> group_hints;
    std::vector<SamplerClass> partition_sampler_class;
};

/**
 * Graph class loads graph from files, and manages all the vertices, edges, partitions, and groups.
 *
 * Partitions are evenly distributed to all availabel NUMA node.
 * Vertices are sorted by degree, except for the vertices of the first few
 * partitions, which are evenly shuffled for load-balance.
 *
 */
class Graph
{
    struct VertexSortUnit {
        vertex_id_t vertex;
        vertex_id_t degree;
    };

    // Sort vertices by degree. Could be faster if parallel sorting is implemented.
    void counting_sort(VertexSortUnit *data, vertex_id_t num) {
        vertex_id_t high = 0;
        #pragma omp parallel for reduction (max: high)
        for (vertex_id_t v_i = 0; v_i < num; v_i++) {
            high = std::max(high, data[v_i].degree);
        }
        VertexSortUnit *temp_data = new VertexSortUnit[num];
        vertex_id_t *counters = new vertex_id_t[high + 1];
        #pragma omp parallel for
        for (vertex_id_t d_i = 0; d_i <= high; d_i++) {
            counters[d_i] = 0;
        }
        #pragma omp parallel for
        for (vertex_id_t v_i = 0; v_i < num; v_i++) {
            temp_data[v_i] = data[v_i];
        }
        for (vertex_id_t v_i = 0; v_i < num; v_i++) {
            assert(data[v_i].degree != 0);
            counters[data[v_i].degree]++;
        }
        for (vertex_id_t d_i = 1; d_i <= high; d_i++) {
            counters[d_i] += counters[d_i - 1];
        }
        for (vertex_id_t v_i = 0; v_i < num; v_i++) {
            vertex_id_t pos = counters[temp_data[v_i].degree - 1]++;
            data[num - pos - 1] = temp_data[v_i];
        }

        delete []temp_data;
        delete []counters;
    }
protected:
    MultiThreadConfig mtcfg;
    MemoryPool mpool;
public:
    std::unique_ptr<AdjList*[]> adjlists; // AdjList [sockets][vertices]
    std::unique_ptr<AdjUnit*[]> edges; // AdjUnit [sockets][vertices / sockets]
    vertex_id_t v_num;
    edge_id_t e_num;
    bool as_undirected;

    vertex_id_t *id2name; // vertex_id_t [vertices] (interleaved)

    vertex_id_t group_num;
    vertex_id_t group_bits;
    vertex_id_t group_mask;
    std::vector<GroupHeader*> groups;
    std::vector<GroupHint> group_hints;

    int partition_num;
    int shuffle_partition_num;
    std::vector<vertex_id_t> partition_begin;
    std::vector<vertex_id_t> partition_end;
    std::vector<SamplerClass> partition_sampler_class;
    std::vector<int> partition_socket;
    std::vector<vertex_id_t> partition_max_degree;
    std::vector<vertex_id_t> partition_min_degree;
    std::vector<edge_id_t> partition_edge_num;
    std::unique_ptr<int*[]> socket_partitions;
    std::unique_ptr<int[]> socket_partition_nums;

    // For node2vec
    std::unique_ptr<BloomFilter> bf;

    // Temporary variables, which will be cleared after making graph.
    std::vector<vertex_id_t> degrees;
    std::vector<Edge> raw_edges;
    std::vector<vertex_id_t> name2id;
    std::vector<VertexSortUnit> vertex_units;
    std::vector<edge_id_t> degree_prefix_sum;

    Graph(MultiThreadConfig _mtcfg) : mpool (_mtcfg) {
        mtcfg = _mtcfg;
        id2name = nullptr;
    }

    ~Graph() {
    }

    int get_vertex_partition_id(vertex_id_t vertex) {
        vertex_id_t g_id = vertex >> group_bits;
        return ((vertex & group_mask) >> groups[0][g_id].partition_bits) + groups[0][g_id].partition_offset;
    }

    int get_partition_group_id(int partition) {
        return partition_begin[partition] >> group_bits;
    }

    void load(const char* path, GraphFormat graph_format, bool _as_undirected = true) {
        LOG(WARNING) << block_begin_str(1) << "Load graph";
        Timer timer;
        as_undirected = _as_undirected;
        e_num = 0;
        v_num = 0;
        if (graph_format == BinaryGraphFormat) {
            read_binary_graph(path, raw_edges);
        } else {
            read_text_graph(path, raw_edges);
        }
        if (as_undirected) {
            e_num = raw_edges.size() * 2;
        } else {
            e_num = raw_edges.size();
        }
        for (edge_id_t e_i = 0; e_i < raw_edges.size(); e_i++) {
            vertex_id_t &a = raw_edges[e_i].src;
            vertex_id_t &b = raw_edges[e_i].dst;
            while (a >= name2id.size()) {
                name2id.push_back(UINT_MAX);
            }
            while (b >= name2id.size()) {
                name2id.push_back(UINT_MAX);
            }
            if (name2id[a] == UINT_MAX) {
                name2id[a] = v_num++;
            }
            if (name2id[b] == UINT_MAX) {
                name2id[b] = v_num++;
            }
            a = name2id[a];
            b = name2id[b];
        }

        LOG(WARNING) << block_mid_str(1) << "Read graph from files in " << timer.duration() << " seconds";
        LOG(WARNING) << block_mid_str(1) << "Vertices number: " << v_num;
        LOG(WARNING) << block_mid_str(1) << "Edges number: " << e_num;
        LOG(WARNING) << block_mid_str(1) << "As undirected: " << (as_undirected ? "true" : "false");

        vertex_units.resize(v_num);
        #pragma omp parallel for
        for (vertex_id_t v_i = 0; v_i < v_num; v_i++) {
            vertex_units[v_i].degree = 0;
            vertex_units[v_i].vertex = v_i;
        }
        #pragma omp parallel for
        for (edge_id_t e_i = 0; e_i < raw_edges.size(); e_i++) {
            __sync_fetch_and_add(&vertex_units[raw_edges[e_i].src].degree, 1);
            if (as_undirected) {
                __sync_fetch_and_add(&vertex_units[raw_edges[e_i].dst].degree, 1);
            }
        }
        Timer sort_timer;
        // std::sort(vertex_units.begin(), vertex_units.end(), [](const VertexSortUnit &a, const VertexSortUnit &b) { return a.degree > b.degree;});
        counting_sort(vertex_units.data(), vertex_units.size());
        LOG(WARNING) << block_mid_str(1) << "Sort graph in " << sort_timer.duration() << " seconds";

        degrees.resize(v_num);
        #pragma omp parallel for
        for (vertex_id_t v_i = 0; v_i < v_num; v_i++) {
            degrees[v_i] = vertex_units[v_i].degree;
        }

        degree_prefix_sum.resize(v_num + 1, 0);
        degree_prefix_sum[0] = 0;
        for (vertex_id_t v_i = 0; v_i < v_num; v_i++) {
            degree_prefix_sum[v_i + 1] = degree_prefix_sum[v_i] + degrees[v_i];
        }
        LOG(WARNING) << block_end_str(1) << "Load graph in " << timer.duration() << " seconds";
    }

    void make(GraphHint* graph_hint) {
        LOG(WARNING) << block_begin_str(1) << "Make edgelists";
        Timer timer;
        group_bits = graph_hint->group_bits;
        group_mask = (1u <<group_bits) - 1;
        group_hints = graph_hint->group_hints;
        partition_sampler_class = graph_hint->partition_sampler_class;
        group_num = graph_hint->group_num;

        groups.resize(mtcfg.socket_num);
        for (int s_i = 0; s_i < mtcfg.socket_num; s_i++) {
            groups[s_i] = mpool.alloc_new<GroupHeader>(group_hints.size());
            for (size_t g_i = 0; g_i < group_hints.size(); g_i++) {
                groups[s_i][g_i].partition_bits = group_hints[g_i].partition_bits;
                if (g_i == 0) {
                    groups[s_i][g_i].partition_offset = 0;
                } else {
                    groups[s_i][g_i].partition_offset = groups[s_i][g_i - 1].partition_offset + bit2value(group_bits - group_hints[g_i - 1].partition_bits);
                }
            }
        }

        partition_num = 0;
        for (vertex_id_t g_i = 0; g_i < group_num; g_i++) {
            auto &hint = group_hints[g_i];
            for (vertex_id_t v = hint.vertex_begin; v < hint.vertex_end; v += bit2value(hint.partition_bits)) {
                partition_begin.push_back(v);
                partition_end.push_back(std::min(v + bit2value(hint.partition_bits), v_num));
                partition_num++;
            }
            if (g_i == 0) {
                // All shuffled partitions belong to group 0
                shuffle_partition_num = std::min(mtcfg.thread_num, partition_num);
            }
        }

        std::vector<vertex_id_t> id2newid(v_num, 0);

        {
            vertex_id_t shuffle_vertex_num = partition_end[shuffle_partition_num - 1];
            std::vector<VertexSortUnit> temp_vertex_units(shuffle_vertex_num);
            std::vector<vertex_id_t> temp_partition_p(shuffle_partition_num);
            std::vector<vertex_id_t> temp_partition_ends(shuffle_partition_num);
            for (vertex_id_t v_i = 0; v_i < shuffle_vertex_num; v_i++) {
                temp_vertex_units[v_i] = vertex_units[v_i];
            }
            for (int p_i = 0; p_i < shuffle_partition_num; p_i++) {
                temp_partition_p[p_i] = partition_begin[p_i];
                temp_partition_ends[p_i] = partition_end[p_i];
            }
            for (vertex_id_t v_i = 0; v_i < shuffle_vertex_num;) {
                for (int p_i = 0; p_i < shuffle_partition_num; p_i++) {
                    int p_idx = p_i;
                    if (v_i < shuffle_vertex_num && temp_partition_p[p_idx] < temp_partition_ends[p_idx]) {
                        vertex_units[temp_partition_p[p_idx]] = temp_vertex_units[v_i];
                        v_i++;
                        temp_partition_p[p_idx]++;
                    }
                }
                for (int p_i = 0; p_i < shuffle_partition_num; p_i++) {
                    int p_idx = shuffle_partition_num - p_i - 1;
                    if (v_i < shuffle_vertex_num && temp_partition_p[p_idx] < temp_partition_ends[p_idx]) {
                        vertex_units[temp_partition_p[p_idx]] = temp_vertex_units[v_i];
                        v_i++;
                        temp_partition_p[p_idx]++;
                    }
                }
            }
        }

        socket_partition_nums.reset(new int[mtcfg.socket_num]);
        for (int s_i = 0; s_i < mtcfg.socket_num; s_i++) {
            socket_partition_nums[s_i] = 0;
        }
        partition_socket.resize(this->partition_num);
        for (int p_i = 0; p_i < this->partition_num; p_i++) {
            if (p_i % (mtcfg.socket_num * 2) < mtcfg.socket_num) {
                partition_socket[p_i] = p_i % mtcfg.socket_num;
            } else {
                partition_socket[p_i] = mtcfg.socket_num - p_i % mtcfg.socket_num - 1;
            }
            socket_partition_nums[partition_socket[p_i]]++;
        }
        socket_partitions.reset(new int*[mtcfg.socket_num]);
        std::vector<vertex_id_t> temp_socket_partition_count(mtcfg.socket_num, 0);
        for (int s_i = 0; s_i < mtcfg.socket_num; s_i++) {
            socket_partitions[s_i] = mpool.alloc<int>(socket_partition_nums[s_i], s_i);
        }
        for (int p_i = 0; p_i < this->partition_num; p_i++) {
            auto socket = partition_socket[p_i];
            socket_partitions[socket][temp_socket_partition_count[socket]++] = p_i;
        }

        partition_edge_num.resize(this->partition_num, 0);
        #pragma omp parallel for
        for (int p_i = 0; p_i < this->partition_num; p_i++) {
            vertex_id_t begin = partition_begin[p_i];
            vertex_id_t end = partition_end[p_i];
            edge_id_t edge_nums = 0;
            for (vertex_id_t v_i = begin; v_i < end; v_i++) {
                edge_nums += vertex_units[v_i].degree;
                id2newid[vertex_units[v_i].vertex] = v_i;
            }
            partition_edge_num[p_i] = edge_nums;
        }

        partition_max_degree.resize(partition_num);
        partition_min_degree.resize(partition_num);
        #pragma omp parallel for
        for (int p_i = 0; p_i < partition_num; p_i++) {
            vertex_id_t max_degree = 0;
            vertex_id_t min_degree = v_num;
            for (vertex_id_t v_i = partition_begin[p_i]; v_i < partition_end[p_i]; v_i ++) {
                max_degree = std::max(max_degree, vertex_units[v_i].degree);
                min_degree = std::min(min_degree, vertex_units[v_i].degree);
            }
            partition_max_degree[p_i] = max_degree;
            partition_min_degree[p_i] = min_degree;
        }

        #pragma omp parallel for
        for (edge_id_t e_i = 0; e_i < raw_edges.size(); e_i++) {
            raw_edges[e_i].src = id2newid[raw_edges[e_i].src];
            raw_edges[e_i].dst = id2newid[raw_edges[e_i].dst];
        }
        LOG(WARNING) << block_mid_str(1) << "Make graph partition in " << timer.duration() << " seconds";
        LOG(WARNING) << block_mid_str(1) << "Partition number: " << partition_num;

        #if PROFILE_IF_DETAIL
        std::stringstream pinfo_ss;
        for (int p_i = 0; p_i < partition_num; p_i++) {
            pinfo_ss << (double)partition_edge_num[p_i] / (partition_end[p_i] - partition_begin[p_i]) << " ";
        }
        LOG(INFO) << "Partition average degree" << (pinfo_ss.str().size() < glog_max_length ? "" : " (message truncated") << ":";
        LOG(INFO) << "\t" << pinfo_ss.str();
        #endif

        id2name = mpool.alloc<vertex_id_t>(v_num, MemoryInterleaved);
        #pragma omp parallel for
        for (size_t n_i = 0; n_i < name2id.size(); n_i++) {
            if (name2id[n_i] != UINT_MAX) {
                name2id[n_i] = id2newid[name2id[n_i]];
                id2name[name2id[n_i]] = n_i;
            }
        }

        adjlists.reset(new AdjList*[mtcfg.socket_num]);
        for (int s_i = 0; s_i < mtcfg.socket_num; s_i++) {
            adjlists[s_i] = mpool.alloc<AdjList>(v_num, s_i);
        }
        #pragma omp parallel for
        for (vertex_id_t v_i = 0; v_i < v_num; v_i++) {
            for (int s_i = 0; s_i < mtcfg.socket_num; s_i++) {
                adjlists[s_i][v_i].degree = vertex_units[v_i].degree;
            }
        }

        edges.reset(new AdjUnit*[mtcfg.socket_num]);
        for (int s_i = 0; s_i < mtcfg.socket_num; s_i++) {
            edge_id_t p_e_num = 0;
            #pragma omp parallel for reduction (+: p_e_num)
            for (int p_i = 0; p_i < socket_partition_nums[s_i]; p_i++) {
                auto partition = socket_partitions[s_i][p_i];
                for (vertex_id_t v_i = partition_begin[partition]; v_i < partition_end[partition]; v_i++) {
                    p_e_num += adjlists[s_i][v_i].degree;
                }
            }
            edges[s_i] = mpool.alloc<AdjUnit>(p_e_num, s_i);
        }
        std::vector<AdjUnit*> edge_end(v_num);
        for (int s_i = 0; s_i < mtcfg.socket_num; s_i++) {
            size_t temp = 0;
            for (int p_i = 0; p_i < socket_partition_nums[s_i]; p_i++) {
                auto partition = socket_partitions[s_i][p_i];
                for (vertex_id_t v_i = partition_begin[partition]; v_i < partition_end[partition]; v_i++) {
                    adjlists[0][v_i].begin = edges[s_i] + temp;
                    edge_end[v_i] = edges[s_i] + temp;
                    temp += adjlists[s_i][v_i].degree;
                }
            }
        }

        #pragma omp parallel for
        for (size_t e_i = 0; e_i < raw_edges.size(); e_i++) {
            vertex_id_t u = raw_edges[e_i].src;
            vertex_id_t v = raw_edges[e_i].dst;
            auto *temp = __sync_fetch_and_add(&edge_end[u], sizeof(AdjUnit));
            temp->neighbor = v;
            if (as_undirected) {
                auto *temp = __sync_fetch_and_add(&edge_end[v], sizeof(AdjUnit));
                temp->neighbor = u;
            }
        }
        for (int s_i = 1; s_i < mtcfg.socket_num; s_i++) {
            #pragma omp parallel for
            for (vertex_id_t v_i = 0; v_i < v_num; v_i++) {
                adjlists[s_i][v_i] = adjlists[0][v_i];
            }
        }

        std::vector<vertex_id_t>().swap(degrees);
        std::vector<Edge>().swap(raw_edges);
        std::vector<vertex_id_t>().swap(name2id);
        std::vector<VertexSortUnit>().swap(vertex_units);
        std::vector<edge_id_t>().swap(degree_prefix_sum);
#if  GCC_VERSION < 40900
        //LOG(WARNING) << "\tMax alignment: " << alignof(max_align_t);
#else
        //LOG(WARNING) << "\tMax alignment: " << alignof(std::max_align_t);
#endif
        LOG(WARNING) << block_mid_str(1) << "Total graph size: " << size_string(get_memory_size());
        LOG(WARNING) << block_end_str(1) << "Make edgelists in " << timer.duration() << " seconds";
    }

    // Create bloom filter for node2vec
    void prepare_neighbor_query() {
        Timer timer;
        #pragma omp parallel for
        for (vertex_id_t v_i = 0; v_i < v_num; v_i++) {
            AdjList* adj= adjlists[0] + v_i;
            std::sort(adj->begin, adj->begin + adj->degree, [](const AdjUnit& a, const AdjUnit& b){return a.neighbor < b.neighbor;});
        }
        bf.reset(new BloomFilter(mtcfg));
        bf->create(as_undirected ? e_num / 2 : e_num);
        #pragma omp parallel for
        for (vertex_id_t v_i = 0; v_i < v_num; v_i++) {
            AdjList* adj = adjlists[0] + v_i;
            for (auto *edge = adj->begin; edge < adj->begin + adj->degree; edge++) {
                bf->insert(v_i, edge->neighbor);
            }
        }
        LOG(WARNING) << block_mid_str() << "Prepare neighborhood query in " << timer.duration() << " seconds";
    }

    // Neighborhood query for node2vec
    bool has_neighbor(vertex_id_t src, vertex_id_t dst, int socket) {
        if (bf->exist(src, dst) == false) {
            return false;
        }
        AdjList* adj = adjlists[socket] + src;
        AdjUnit unit;
        unit.neighbor = dst;
        return std::binary_search(adj->begin, adj->begin + adj->degree, unit, [](const AdjUnit &a, const AdjUnit &b) { return a.neighbor < b.neighbor; });
    }

    size_t get_memory_size() {
        return sizeof(AdjList) * v_num * (size_t) mtcfg.socket_num + sizeof(AdjUnit) * e_num;
    }

    size_t get_csr_size() {
        return sizeof(AdjList) * v_num + sizeof(AdjUnit) * e_num;
    }
};
