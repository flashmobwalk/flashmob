#pragma once

#include <immintrin.h>

#include "log.hpp"
#include "graph.hpp"
#include "profiler.hpp"
#include "type.hpp"
#include "memory.hpp"

/**
 * Each Sampler instance manages edge sampling for one partition.
 *
 * The base class contains common variables and interfaces for all
 * sampler types.
 */
class Sampler {
public:
    AdjList* adjlists;
    vertex_id_t vertex_begin;
    vertex_id_t vertex_end;
    SamplerClass sampler_class;

    Sampler () {
        adjlists = nullptr;
        vertex_begin = 0;
        vertex_end = 0;
        sampler_class = ClassBaseSampler;
    }

    virtual ~Sampler() {
    }

    /**
     * Return the suggested edge buffer size for a given vertex.
     *
     * If the edge buffer is too small, it needs to be frequently
     * refilled, whose overhead offset the benefit of using the buffer.
     * The buffer size cannot be a power of 2 because of cache-associativity
     * problem.
     *
     */
    vertex_id_t get_edge_buffer_length(vertex_id_t vertex) {
        vertex_id_t len = adjlists[vertex].degree;
        if (len < 8) {
            len = 8;
        }
        if (len > 8 && (len & (len - 1)) == 0) {
            len ++;
        }
        return len;
    }
};

/**
 * In ExlusiveBufferSampler, each vertex has a consecutive edge buffer.
 *
 */
class ExclusiveBufferSampler: public Sampler {
public:
    struct EdgeBufferHeader {
        uint32_t head;
        uint32_t end;
    };
    vertex_id_t *units;
    EdgeBufferHeader *headers;
    uint64_t buffer_unit_num;

    ExclusiveBufferSampler () {
        units = nullptr;
        headers = nullptr;
        buffer_unit_num = 0;
        sampler_class = ClassExclusiveBufferSampler;
    }

    virtual ~ExclusiveBufferSampler() {
    }

    void clear() {
        for (vertex_id_t v_i = vertex_begin; v_i < vertex_end; v_i++) {
            vertex_id_t v_idx = v_i - vertex_begin;
            headers[v_idx].head = headers[v_idx].end;
        }
    }

    vertex_id_t sample(const vertex_id_t vertex, default_rand_t *rd) {
        const vertex_id_t v_idx = vertex - vertex_begin;
        auto &h = headers[v_idx];
        if (h.head == h.end) {
            fill(vertex, rd);
        }
        uint32_t edge_idx = h.head++;
        vertex_id_t ret = units[edge_idx];
        if ((edge_idx & 15) == 15) {
            _mm_prefetch(&units[edge_idx + 1], _MM_HINT_T1);
            // _mm_clflushopt(&units[edge_idx]);
        }
        return ret;
    }

    void fill(vertex_id_t vertex, default_rand_t *rd) {
        vertex_id_t v_idx = vertex - vertex_begin;
        auto &h = headers[v_idx];
        AdjList *adjlist = adjlists + vertex;
        AdjUnit *adjunits = adjlist->begin;
        vertex_id_t degree = adjlist->degree;

        vertex_id_t *p_begin = units + (h.end - get_edge_buffer_length(vertex));
        vertex_id_t *p_end = units + h.head;
        vertex_id_t fill_edge_num = p_end - p_begin;

        for (vertex_id_t e_i = 0; e_i < fill_edge_num; e_i ++) {
            p_begin[e_i] = adjunits[rd->gen(degree)].neighbor;
        }
        for (vertex_id_t e_i = 0; e_i < degree; e_i += CacheLineSize / sizeof(AdjUnit)) {
            _mm_clflush(&adjunits[e_i]);
        }
        h.head = h.end - get_edge_buffer_length(vertex);
    }

    void init(vertex_id_t _vertex_begin, vertex_id_t _vertex_end, AdjList* _adjlists, MemoryPool* mpool, int socket) {
        vertex_begin = _vertex_begin;
        vertex_end = _vertex_end;
        adjlists = _adjlists;
        auto sampler_vertex_num = vertex_end - vertex_begin;

        buffer_unit_num = 0;
        for (vertex_id_t v_i = vertex_begin; v_i < vertex_end; v_i++) {
            buffer_unit_num += get_edge_buffer_length(v_i);
        }

        MemoryCounter mcounter;
        mcounter.al_alloc<EdgeBufferHeader> (sampler_vertex_num);
        mcounter.al_alloc<vertex_id_t> (buffer_unit_num);
        mcounter.align();
        auto memory = mpool->get_memory(&mcounter, socket);
        headers = memory->al_alloc_new<EdgeBufferHeader>(sampler_vertex_num);
        units = memory->al_alloc_new<vertex_id_t>(buffer_unit_num);
        memory->align();

        vertex_id_t buffer_unit_p = 0;
        for (vertex_id_t v_idx = 0; v_idx < sampler_vertex_num; v_idx++) {
            headers[v_idx].end = buffer_unit_p + get_edge_buffer_length(v_idx + vertex_begin);
            headers[v_idx].head = headers[v_idx].end;
            buffer_unit_p = headers[v_idx].end;
        }
    }

    /**
     * Flushes all the data out of cache.
     *
     * This function is only used in performance profiling where the data will be repeatedly
     * accessed to measure the average sampling overhead. Allowing data residing in the cache
     * may affect the accuracy of the measurement.
     *
     */
    void reset(vertex_id_t _vertex_begin, vertex_id_t _vertex_end, AdjList *_adjlists) {
        for (vertex_id_t e_i = 0; e_i < buffer_unit_num; e_i += CacheLineSize / sizeof(units[0])) {
            _mm_clflush(&units[e_i]);
        }
        for (vertex_id_t v_i = vertex_begin; v_i < vertex_end; v_i += CacheLineSize / sizeof(headers[0])) {
            _mm_clflush(&headers[v_i - vertex_begin]);
        }
        for (vertex_id_t v_i = vertex_begin; v_i < vertex_end; v_i++) {
            for (vertex_id_t e_i = 0; e_i < adjlists[v_i].degree; e_i += CacheLineSize / sizeof(AdjUnit)) {
                _mm_clflush(&adjlists[v_i].begin[e_i]);
            }
        }
        for (vertex_id_t v_i = vertex_begin; v_i < vertex_end; v_i += CacheLineSize / sizeof(AdjList)) {
            _mm_clflush(&adjlists[v_i]);
        }
        vertex_begin = _vertex_begin;
        vertex_end = _vertex_end;
        adjlists = _adjlists;
    }
};

/**
 * Direct samples edges from the graph.
 *
 * First the degree and adjacency list of the vertex are fetched,
 * then sample from the adjacency list.
 *
 */
class DirectSampler: public Sampler {
public:
    DirectSampler() {
        sampler_class = ClassDirectSampler;
    }

    virtual ~DirectSampler() {
    }

    vertex_id_t sample(vertex_id_t vertex, default_rand_t *rd) {
        vertex_id_t degree = adjlists[vertex].degree;
        return adjlists[vertex].begin[rd->gen(degree)].neighbor;
    }

    void init(vertex_id_t _vertex_begin, vertex_id_t _vertex_end, AdjList* _adjlists) {
        vertex_begin = _vertex_begin;
        vertex_end = _vertex_end;
        adjlists = _adjlists;
    }
};

/**
 * Direct samples edges from the graph.
 *
 * This sampler requires all the vertices have the same degree within the partitions.
 * The vertex degree and address of adjacency list of the vertex can be calculated
 * based on the vertex ID.
 *
 */
class UniformDegreeDirectSampler: public Sampler {
    vertex_id_t degree;
    AdjUnit *edge_begin;
public:
    UniformDegreeDirectSampler() {
        degree = 0;
        edge_begin = nullptr;
        sampler_class = ClassUniformDegreeDirectSampler;
    }

    virtual ~UniformDegreeDirectSampler() {
    }

    vertex_id_t sample(vertex_id_t vertex, default_rand_t *rd) {
        vertex_id_t v_idx = vertex - vertex_begin;
        return edge_begin[v_idx * degree + rd->gen(degree)].neighbor;
    }

    void init(vertex_id_t _vertex_begin, vertex_id_t _vertex_end, AdjList *_adjlists) {
        vertex_begin = _vertex_begin;
        vertex_end = _vertex_end;
        adjlists = _adjlists;
        degree = adjlists[vertex_begin].degree;
        edge_begin = adjlists[vertex_begin].begin;
    }

    /**
     * Flushes all the data out of cache.
     *
     * This function is only used in performance profiling where the data will be repeatedly
     * accessed to measure the average sampling overhead. Allowing data residing in the cache
     * may affect the accuracy of the measurement.
     *
     */
    void reset(vertex_id_t _vertex_begin, vertex_id_t _vertex_end, AdjList *_adjlists) {
        AdjUnit *edge_end = edge_begin + degree * (vertex_end - vertex_begin);
        for (AdjUnit *p = edge_begin; p < edge_end; p += 16) {
            _mm_clflush(p);
        }
        init(_vertex_begin, _vertex_end, _adjlists);
    }
};

/**
 * Direct samples edges from the graph.
 *
 * This sampler requires all the vertices have similar degree within the partitions.
 * The vertex degree and address of adjacency list of the vertex can be calculated
 * based on the vertex ID.
 *
 */
class SimilarDegreeDirectSampler: public Sampler {
    struct AdjHint {
        vertex_id_t vertex_begin;
        vertex_id_t vertex_end;
        vertex_id_t degree;
        AdjUnit *edge_begin;
    };
    vertex_id_t hint_num;
    AdjHint hints[SimilarDegreeDirectSamplerMaxHintNum];
public:
    SimilarDegreeDirectSampler() {
        sampler_class = ClassSimilarDegreeDirectSampler;
    }

    virtual ~SimilarDegreeDirectSampler() {
    }

    /**
     * Validate if this sampler is suitable for partition pid.
     *
     * Only use this sampler when vertices degree are similar within the partition,
     * and the partition data cannot fit into L2 cache. The vertices within the partion
     * must be sorted by their degree in ascending order.
     *
     */
    static bool valid(int pid, uint64_t l2_cache_size, Graph* graph) {
        if (graph->partition_max_degree[pid] - graph->partition_min_degree[pid] + 1 > SimilarDegreeDirectSamplerMaxHintNum) {
            return false;
        }
        if (graph->partition_edge_num[pid] * sizeof(AdjUnit) + (graph->partition_end[pid] - graph->partition_begin[pid]) * sizeof(AdjList) < l2_cache_size) {
            return false;
        }
        #ifdef UNIT_TEST
        for (vertex_id_t v_i = graph->partition_begin[pid]; v_i + 1 < graph->partition_end[pid]; v_i++) {
            CHECK(graph->adjlists[0][v_i].degree >= graph->adjlists[0][v_i + 1].degree);
        }
        #endif
        return true;
    }

    vertex_id_t sample(vertex_id_t vertex, default_rand_t *rd) {
        for (vertex_id_t h_i = 0; h_i < hint_num; h_i++) {
            auto &hint = hints[h_i];
            if (vertex < hint.vertex_end) {
                return hint.edge_begin[(vertex - hint.vertex_begin) * hint.degree + rd->gen(hint.degree)].neighbor;
            }
        }
        return 0;
    }

    void init(vertex_id_t _vertex_begin, vertex_id_t _vertex_end, AdjList *_adjlists) {
        vertex_begin = _vertex_begin;
        vertex_end = _vertex_end;
        adjlists = _adjlists;
        hint_num = 0;
        for (vertex_id_t v_i = vertex_begin, current_degree = 0; v_i < vertex_end ; v_i++) {
            if (current_degree != adjlists[v_i].degree) {
                current_degree = adjlists[v_i].degree;
                hints[hint_num].degree = current_degree;
                hints[hint_num].vertex_begin = v_i;
                hints[hint_num].edge_begin = adjlists[v_i].begin;
                if (hint_num!= 0) {
                    hints[hint_num - 1].vertex_end = v_i;
                }
                hint_num++;
            }
        }
        hints[hint_num - 1].vertex_end = vertex_end;
    }
};

/**
 * Manages all the samplers.
 *
 * The samplers are selected based on the dynamic programming results.
 *
 */
class SamplerManager
{
protected:
    MemoryPool mpool;

    // members are not owned by this class
    Graph *graph;
    SampleProfiler *profiler;

    MultiThreadConfig mtcfg;
public:
    std::vector<Sampler*> samplers;

    void clear() {
        #pragma omp parallel for
        for (int p_i = 0; p_i < graph->partition_num; p_i++) {
            auto *sampler = samplers[p_i];
            switch(sampler->sampler_class) {
                case ClassExclusiveBufferSampler: static_cast<ExclusiveBufferSampler*>(sampler)->clear(); break;
                case ClassDirectSampler: break;
                case ClassUniformDegreeDirectSampler: break;
                default: CHECK(false);
            }
        }
    }

    SamplerManager(MultiThreadConfig _mtcfg) : mpool(_mtcfg) {
        mtcfg = _mtcfg;
        profiler = nullptr;
        graph = nullptr;
    }

    ~SamplerManager() {
    }

    void init(Graph *_graph, walker_id_t max_epoch_walker_num, SampleProfiler *_profiler) {
        Timer timer;
        graph = _graph;
        profiler = _profiler;

        samplers.resize(graph->partition_num);

        auto &edge_buffer_data_size = profiler->edge_buffer_data_size;
        edge_buffer_data_size = 0;
#pragma omp parallel for reduction (+: edge_buffer_data_size)
        for (int p_i = 0; p_i < graph->partition_num; p_i++) {
            auto &sampler_class = graph->partition_sampler_class[p_i];
            if (sampler_class == ClassExclusiveBufferSampler) {
                auto* sampler = mpool.alloc_new<ExclusiveBufferSampler>(1, graph->partition_socket[p_i]);
                sampler->init(graph->partition_begin[p_i], graph->partition_end[p_i], graph->adjlists[graph->partition_socket[p_i]], &mpool, graph->partition_socket[p_i]);
                samplers[p_i] = sampler;
                edge_buffer_data_size += sampler->buffer_unit_num;
            } else if (graph->partition_min_degree[p_i] == graph->partition_max_degree[p_i]) {
                auto* sampler = mpool.alloc_new<UniformDegreeDirectSampler>(1, graph->partition_socket[p_i]);
                sampler->init(graph->partition_begin[p_i], graph->partition_end[p_i], graph->adjlists[graph->partition_socket[p_i]]);
                samplers[p_i] = sampler;
            } else if (SimilarDegreeDirectSampler::valid(p_i, mtcfg.l2_cache_size, graph)) {
                auto* sampler = mpool.alloc_new<SimilarDegreeDirectSampler>(1, graph->partition_socket[p_i]);
                sampler->init(graph->partition_begin[p_i], graph->partition_end[p_i], graph->adjlists[graph->partition_socket[p_i]]);
                samplers[p_i] = sampler;
            } else {
                auto* sampler = mpool.alloc_new<DirectSampler>(1, graph->partition_socket[p_i]);
                sampler->init(graph->partition_begin[p_i], graph->partition_end[p_i], graph->adjlists[graph->partition_socket[p_i]]);
                samplers[p_i] = sampler;
            }
        }

#pragma omp parallel for
        for (int p_i = 0; p_i < graph->partition_num; p_i++) {
            #if PROFILE_IF_NORMAL
            auto group = graph->get_partition_group_id(p_i);
            __sync_fetch_and_add(&(profiler->group_vertex_num[group]), graph->partition_end[p_i] - graph->partition_begin[p_i]);
            #endif
            #if PROFILE_IF_NORMAL
            __sync_fetch_and_add(&(profiler->partition_vertex_num[p_i]), graph->partition_end[p_i] - graph->partition_begin[p_i]);
            profiler->partition_sampler_class[p_i] = samplers[p_i]->sampler_class;
            uint64_t edge_num = 0;
            for (vertex_id_t v_i = graph->partition_begin[p_i]; v_i < graph->partition_end[p_i]; v_i++) {
                edge_num += graph->adjlists[0][v_i].degree;
            }
            __sync_fetch_and_add(&(profiler->partition_edge_num[p_i]), edge_num);
            #endif
        }
        LOG(WARNING) << block_mid_str() << "Initialize samplers in " << timer.duration() << " seconds";
    }
};
