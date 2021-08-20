#pragma once

#include <numa.h>

#include <iostream>
#include <thread>
#include <memory>

#include <args.hxx>

#include "type.hpp"
#include "log.hpp"
#include "numa_helper.hpp"
#include "sysinfo.hpp"

/**
 * The parsers and helpers are used for parameter parsing
 */

class OptionParser
{
protected:
    //The order of class member variable initiation
    //depends on the order of member variable declaration in the class
    //so parser should be placed before any other args' flags
    args::ArgumentParser parser;
private:
    args::HelpFlag help;
public:
    OptionParser() :
        parser("", ""),
        help(parser, "help", "Display this help menu", {'h', "help"})
    {}

    virtual void parse(int argc, char **argv)
    {
        // LOG(WARNING) << block_begin_str() << "Parse cmd";
        try
        {
            parser.ParseCLI(argc, argv);
        }
        // Exceptions should be caught by constant reference, not by value.
        catch (const args::Help &)
        {
            std::cout << parser;
            exit(0);
        }
        catch (const args::ParseError &e)
        {
            std::cerr << e.what() << std::endl;
            std::cerr << parser;
            exit(1);
        }
        catch (const args::ValidationError &e)
        {
            std::cerr << e.what() << std::endl;
            std::cerr << parser;
            exit(1);
        }
    }
};

class InOutOptionHelper
{
private:
    args::ValueFlag<std::string> input_flag;
    args::ValueFlag<std::string> output_flag;
public:
    std::string input;
    std::string output;
    InOutOptionHelper(args::ArgumentParser &parser):
        input_flag(parser, "input", "input", {'i'}),
        output_flag(parser, "output", "output", {'o'})
    {}
    virtual void parse()
    {
        CHECK(input_flag);
        CHECK(output_flag);
        input = args::get(input_flag);
        output = args::get(output_flag);
        LOG(WARNING) << block_mid_str() << "Input: " << input;
        LOG(WARNING) << block_mid_str() << "Output: " << output;
    }
};

class FormatOptionHelper
{
private:
    args::ValueFlag<std::string> graph_format_flag;
public:
    GraphFormat graph_format;
    FormatOptionHelper(args::ArgumentParser &parser):
        graph_format_flag(parser, "format", "graph format: binary | text", {'f'})
    {}
    virtual void parse()
    {
        CHECK(graph_format_flag);
        std::string graph_format_str = args::get(graph_format_flag);
        if (graph_format_str == "binary") {
            graph_format = BinaryGraphFormat;
        } else if (graph_format_str == "text") {
            graph_format = TextGraphFormat;
        } else {
            std::cerr << "[error] Unknown graph format: " << graph_format_str << std::endl;
            exit(1);
        }
        LOG(WARNING) << block_mid_str() << "Graph format: " << graph_format_str;
    }
};

class ThreadsOptionHelper
{
private:
    args::ValueFlag<int> thread_num_flag;
public:
    MultiThreadConfig mtcfg;
    ThreadsOptionHelper(args::ArgumentParser &parser):
       thread_num_flag(parser, "threads", "[optional] number of threads this program will use", {'t'}) {
    }
    virtual void parse()
    {
        if (!thread_num_flag) {
            mtcfg.thread_num = get_max_core_num();
        } else {
            mtcfg.thread_num = args::get(thread_num_flag);
        }
        mtcfg.socket_num = 1;
        LOG(WARNING) << block_mid_str() << "Thread number: " << mtcfg.thread_num;
    }
};

class NumaOptionHelper
{
private:
    args::ValueFlag<int> thread_num_flag;
    args::ValueFlag<int> socket_num_flag;
    args::ValueFlag<std::string> socket_mapping_flag;
    args::ValueFlag<uint64_t> mem_quota_flag;
public:
    MultiThreadConfig mtcfg;
    uint64_t mem_quota;
    NumaOptionHelper(args::ArgumentParser &parser):
        thread_num_flag(parser, "threads", "[optional] number of threads this program will use", {'t'}),
        socket_num_flag(parser, "sockets", "[optional] number of sockets", {'s'}),
        socket_mapping_flag(parser, "socket-mapping", "[optional] example: --socket-mapping=0,1,2,3", {"socket-mapping"}),
        mem_quota_flag(parser, "mem", "[optional] Maximum memory this program will use (in GiB)", {"mem"})
    {}
    virtual void parse() {
        if (socket_num_flag) {
            mtcfg.socket_num = args::get(socket_num_flag);
        } else {
            mtcfg.socket_num = get_max_socket_num();
        }
        LOG(WARNING) << block_mid_str() << "Sockets: " << mtcfg.socket_num;

        if (thread_num_flag) {
            mtcfg.thread_num = args::get(thread_num_flag);
        } else {
            mtcfg.thread_num = get_max_core_num() / get_max_socket_num() * mtcfg.socket_num;
        }
        LOG(WARNING) << block_mid_str() << "Thread number: " << mtcfg.thread_num;

        if (socket_mapping_flag) {
            std::vector<int> vect;
            std::stringstream ss(args::get(socket_mapping_flag));
            for (int i; ss >> i;) {
                vect.push_back(i);
                if (ss.peek() == ',')
                    ss.ignore();
            }
            CHECK((int) vect.size() == mtcfg.socket_num);
            mtcfg.set_socket_mapping(vect);

        } else {
            mtcfg.set_default_socket_mapping();
        }
        std::stringstream smp_ss;
        for (int s_i = 0; s_i < mtcfg.socket_num; s_i++) {
            // printf("%d %d\n", mtcfg.get_socket_mapping(s_i), get_max_socket_num());
            CHECK(mtcfg.get_socket_mapping(s_i) < get_max_socket_num());
            if (s_i != 0) {
                smp_ss << ", ";
            }
            smp_ss << s_i << "->" << mtcfg.get_socket_mapping(s_i);
        }
        LOG(WARNING) << block_mid_str() << "Socket mapping: " << smp_ss.str();

        uint64_t sys_mem = get_sys_mem();
        uint64_t os_mem = (1ull << 31);
        CHECK(sys_mem >= os_mem) << "System memory must be no smaller thaln " << size_string(os_mem);
        if (mem_quota_flag) {
            mem_quota = args::get(mem_quota_flag) << 30u;
            CHECK(mem_quota <= sys_mem * mtcfg.socket_num / get_max_socket_num()) << "Not enough memory: assigned " << size_string(mem_quota) \
                << ", only " << size_string(sys_mem * mtcfg.socket_num / get_max_socket_num()) << " on " << mtcfg.socket_num << " sockets";
        } else {
            mem_quota = (sys_mem - os_mem) * 0.9 * mtcfg.socket_num / get_max_socket_num();
        }
        LOG(WARNING) << block_mid_str() << "Assigned memory: " << size_string(mem_quota) << " out of " << size_string(sys_mem);

        mtcfg.l2_cache_size = get_l2_cache_size();
        LOG(WARNING) << block_mid_str() << "L2 cache size: " << size_string(mtcfg.l2_cache_size);
    }
};

class GraphOptionHelper : public FormatOptionHelper
{
private:
    args::ValueFlag<std::string> graph_path_flag;
public:
    std::string graph_path;
    GraphOptionHelper(args::ArgumentParser &parser):
        FormatOptionHelper(parser),
        graph_path_flag(parser, "graph", "graph path", {'g'})
    {
    }
    virtual void parse() override {
        FormatOptionHelper::parse();

        CHECK(graph_path_flag);
        graph_path = args::get(graph_path_flag);

        LOG(WARNING) << block_mid_str() << "Graph path: " << graph_path;
    }
};

class WalkOptionHelper
{
private:
    args::ValueFlag<int> epoch_num_flag;
    args::ValueFlag<uint64_t> walker_num_flag;
    args::ValueFlag<int> walk_len_flag;
public:
    int epoch_num;
    uint64_t walker_num;
    int walk_len;
    WalkOptionHelper(args::ArgumentParser &parser):
        epoch_num_flag(parser, "epoch", "walk epoch number", {'e'}),
        walker_num_flag(parser, "walker", "walker number", {'w'}),
        walk_len_flag(parser, "length", "walk length", {'l'})
    {
    }
    virtual void parse() {
        CHECK(epoch_num_flag || walker_num_flag);
        CHECK(!(epoch_num_flag && walker_num_flag));
        if (epoch_num_flag) {
            epoch_num = args::get(epoch_num_flag);
            LOG(WARNING) << block_mid_str() << "Epoch number: " << epoch_num;
        } else {
            epoch_num = 0;
        }
        if (walker_num_flag) {
            walker_num = args::get(walker_num_flag);
            LOG(WARNING) << block_mid_str() << "Walker number: " << walker_num;
        } else {
            walker_num = 0;
        }

        CHECK(walk_len_flag);
        walk_len = args::get(walk_len_flag);
        LOG(WARNING) << block_mid_str() << "Walk length: " << walk_len;
    }

    uint64_t get_walker_num(vertex_id_t vertex_num) {
        if (walker_num != 0) {
            return walker_num;
        } else {
            return (uint64_t) epoch_num * vertex_num;
        }
    }

    std::function<uint64_t(vertex_id_t, edge_id_t)> get_walker_num_func() {
        std::function<uint64_t(vertex_id_t, edge_id_t)> f = [=](vertex_id_t vertex_num, edge_id_t) {
            return this->get_walker_num(vertex_num);
        };
        return f;
    }
};

class InOutOptionParser: public OptionParser, public InOutOptionHelper
{
public:
    InOutOptionParser():
        OptionParser(),
        InOutOptionHelper(parser)
    {}
    virtual void parse(int argc, char **argv)
    {
        OptionParser::parse(argc, argv);
        InOutOptionHelper::parse();
    }
};

class NumaOptionParser: public OptionParser, public NumaOptionHelper
{
public:
    NumaOptionParser():
        OptionParser(),
        NumaOptionHelper(parser)
    {}
    virtual void parse(int argc, char** argv) override {
        OptionParser::parse(argc, argv);
        NumaOptionHelper::parse();
    }
};

class GraphLoadOptionParser: public OptionParser, public GraphOptionHelper
{
public:
    GraphLoadOptionParser():
        OptionParser(),
        GraphOptionHelper(parser)
    {}
    virtual void parse(int argc, char** argv) override {
        OptionParser::parse(argc, argv);
        GraphOptionHelper::parse();
    }
};

class GraphOptionParser: public OptionParser, public NumaOptionHelper, public GraphOptionHelper
{
public:
    GraphOptionParser():
        OptionParser(),
        NumaOptionHelper(parser),
        GraphOptionHelper(parser)
    {}
    virtual void parse(int argc, char** argv) override {
        OptionParser::parse(argc, argv);
        NumaOptionHelper::parse();
        GraphOptionHelper::parse();
    }
};

class WalkOptionParser: public OptionParser, public NumaOptionHelper, public GraphOptionHelper, public WalkOptionHelper
{
public:
    WalkOptionParser():
        OptionParser(),
        NumaOptionHelper(parser),
        GraphOptionHelper(parser),
        WalkOptionHelper(parser)
    {}
    virtual void parse(int argc, char** argv) override {
       OptionParser::parse(argc, argv);
       NumaOptionHelper::parse();
       GraphOptionHelper::parse();
       WalkOptionHelper::parse();
    }
};

class Node2vecOptionHelper
{
private:
    args::ValueFlag<real_t> p_flag;
    args::ValueFlag<real_t> q_flag;
public:
    real_t p;
    real_t q;
    Node2vecOptionHelper(args::ArgumentParser &parser):
        p_flag(parser, "p", "node2vec parameter p", {'p'}),
        q_flag(parser, "q", "node2vec parameter q", {'q'})
    {
    }
    virtual void parse() {
        CHECK(p_flag);
        p = args::get(p_flag);
        LOG(WARNING) << block_mid_str() << "p: " << p;

        CHECK(q_flag);
        q = args::get(q_flag);
        LOG(WARNING) << block_mid_str() << "q: " << q;
    }
};

class Node2vecOptionParser: public WalkOptionParser, public Node2vecOptionHelper
{
public:
    Node2vecOptionParser():
        WalkOptionParser(),
        Node2vecOptionHelper(parser)
    {}
    virtual void parse(int argc, char** argv) override {
       WalkOptionParser::parse(argc, argv);
       Node2vecOptionHelper::parse();
    }
};
