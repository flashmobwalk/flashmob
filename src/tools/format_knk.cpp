#include <assert.h>

#include <sstream>
#include <map>

#include "option.hpp"
#include "log.hpp"
#include "io.hpp"

class FormatKnKOptionHelper : public OptionParser
{
private:
    args::ValueFlag<std::string> input_path_flag;
    args::ValueFlag<std::string> output_path_flag;
public:
    std::string input_path;
    std::string output_path;
    FormatKnKOptionHelper():
        input_path_flag(parser, "input", "input path", {'i'}),
        output_path_flag(parser, "output", "output path", {'o'})
    {}
    virtual void parse(int argc, char **argv)
    {
        OptionParser::parse(argc, argv);

        assert(input_path_flag);
        input_path = args::get(input_path_flag);
        LOG(INFO) << "input: " << input_path;

        assert(output_path_flag);
        output_path = args::get(output_path_flag);
        LOG(INFO) << "output: " << output_path;
    }
};

void format_knk(std::string input_path, std::string output_path) {
    std::vector<Edge> edges;
    read_text_graph(input_path.c_str(), edges);

    std::map<vertex_id_t, vertex_id_t> name2id;
    auto get_vertex_id = [&] (vertex_id_t name) {
        if (name2id.find(name) == name2id.end()) {
            vertex_id_t id = name2id.size();
            name2id[name] = id;
        }
        return name2id[name];
    };
    for (auto &e : edges) {
        e.src = get_vertex_id(e.src);
        e.dst = get_vertex_id(e.dst);
    }
    std::stringstream ss;
    ss << "# Converted from: " << input_path << std::endl;
    ss << "# vertex number: " << name2id.size() << std::endl;
    ss << "# edegs: " << edges.size() << std::endl;
    write_binary_graph(output_path.c_str(), edges, ss);
}

int main(int argc, char** argv)
{
    init_glog(argv, google::INFO);

    FormatKnKOptionHelper opt;
    opt.parse(argc, argv);

    format_knk(opt.input_path, opt.output_path);
    return 0;
}
