#include <glog/logging.h>

#include "option.hpp"
#include "graph.hpp"
#include "numa_helper.hpp"
#include "solver.hpp"

int main(int argc, char** argv)
{
    init_glog(argv, google::INFO);

    Node2vecOptionParser opt;
    opt.parse(argc, argv);

    init_concurrency(opt.mtcfg);

    Graph graph(opt.mtcfg);
    make_graph(opt.graph_path.c_str(), opt.graph_format, true, opt.get_walker_num_func(), opt.walk_len, opt.mtcfg, opt.mem_quota, true, graph);

    FMobSolver solver(&graph, opt.mtcfg);
    solver.set_node2vec(opt.p, opt.q);
    walk(&solver, opt.get_walker_num(graph.v_num), opt.walk_len, opt.mem_quota);
    return 0;
}
