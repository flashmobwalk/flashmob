#include "option.hpp"
#include "numa_helper.hpp"
#include "log.hpp"

#include "graph.hpp"
#include "solver.hpp"
#include "partition.hpp"

int main(int argc, char** argv)
{
    init_glog(argv, google::INFO);

    WalkOptionParser opt;
    opt.parse(argc, argv);

    init_concurrency(opt.mtcfg);

    Graph graph(opt.mtcfg);
    make_graph(opt.graph_path.c_str(), opt.graph_format, true, opt.get_walker_num_func(), opt.walk_len, opt.mtcfg, opt.mem_quota, false, graph);

    FMobSolver solver(&graph, opt.mtcfg);
    walk(&solver, opt.get_walker_num(graph.v_num), opt.walk_len, opt.mem_quota);
    return 0;
}
