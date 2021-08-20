#pragma once

#include <stdio.h>
#include <sstream>
#include <vector>

#include "type.hpp"
#include "log.hpp"
#include "compile_helper.hpp"

/**
 * The raw edge type, as well as the read/write functions are used to
 * read/write edges from files
 */

struct Edge {
    vertex_id_t src;
    vertex_id_t dst;
    Edge() {}
    Edge(vertex_id_t _src, vertex_id_t _dst) : src(_src), dst(_dst) {}
    bool friend operator == (const Edge &a, const Edge &b)
    {
        return (a.src == b.src
            && a.dst == b.dst
        );
    }
    void transpose()
    {
        std::swap(src, dst);
    }
};

std::string get_info_graph_path(std::string fname)
{
    std::string info_path = std::string(fname) + ".info.txt";
    return info_path;
}

template<typename T>
void read_binary_graph(const char* fname, std::vector<T> &edges) {
    FILE *f = fopen(fname, "r");
    CHECK(f != NULL);
    fseek(f, 0, SEEK_END);
    size_t total_size = ftell(f);
    size_t total_e_num = total_size / sizeof(T);
	edges.resize(total_e_num);

    fseek(f, 0, SEEK_SET);
    auto ret = fread(edges.data(), sizeof(T), total_e_num, f);
    CHECK(ret == total_e_num);
    _unused(ret);
    fclose(f);
}

template<typename T>
void write_binary_graph(const char* fname, std::vector<T> &edges) {
    FILE *out_f = fopen(fname, "w");
    CHECK(out_f != NULL);

    auto ret = fwrite(edges.data(), sizeof(T), edges.size(), out_f);
    _unused(ret);
    CHECK(ret == edges.size());
    fclose(out_f);
}

template<typename T>
void write_binary_graph(const char* fname, std::vector<T> &edges, std::stringstream &ss) {
    std::string info_path = get_info_graph_path(std::string(fname));
    FILE *out_f = fopen(info_path.c_str(), "w");
    CHECK(out_f != NULL);
    fprintf(out_f, "%s", ss.str().c_str());
    fclose(out_f);
    write_binary_graph(fname, edges);
}

void read_text_graph(const char* fname, std::vector<Edge> &edges) {
    edges.clear();
	vertex_id_t a, b;
	char temp_str[200];
    FILE *f = fopen(fname, "r");
    CHECK(f != NULL);
	while (fgets(temp_str, 200, f)) {
		if (temp_str[0] == '#') {
			continue;
		}
		sscanf(temp_str, "%u %u", &a, &b);
		edges.push_back(Edge(a, b));
	}
    fclose(f);
}

void write_text_graph(const char* fname, std::vector<Edge> &edges, std::stringstream &ss) {
    FILE *out_f = fopen(fname, "w");
    CHECK(out_f != NULL);
    fprintf(out_f, "%s", ss.str().c_str());
    for (auto &e : edges) {
        fprintf(out_f, "%u %u\n", e.src, e.dst);
    }
    fclose(out_f);
}

void write_text_graph(const char* fname, std::vector<Edge> &edges) {
    std::stringstream ss;
    write_text_graph(fname, edges, ss);
}
