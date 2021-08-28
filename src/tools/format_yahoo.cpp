#include <sstream>
#include <fstream>
#include <map>

#include "option.hpp"
#include "log.hpp"
#include "io.hpp"

class FormatYahooOptionHelper : public OptionParser
{
private:
    args::ValueFlagList<std::string> input_paths_flag;
    args::ValueFlag<std::string> binary_output_path_flag;
    args::ValueFlag<std::string> text_output_path_flag;
public:
    std::vector<std::string> input_paths;
    std::string binary_output_path;
    std::string text_output_path;
    FormatYahooOptionHelper():
        OptionParser(),
        input_paths_flag(parser, "input", "input paths", {'i'}),
        binary_output_path_flag(parser, "binary output", "binary output path", {'b'}),
        text_output_path_flag(parser, "text output", "text output path", {'t'})
    {}
    virtual void parse(int argc, char **argv)
    {
        OptionParser::parse(argc, argv);

        CHECK(input_paths_flag);
        for (const auto path: args::get(input_paths_flag)) {
            input_paths.push_back(path);
            LOG(INFO) << "Input file: " << path;
        }

        CHECK(binary_output_path_flag || text_output_path_flag);
        if (binary_output_path_flag) {
            binary_output_path = args::get(binary_output_path_flag);
            LOG(INFO) << "Output as binary format: " << binary_output_path;
        }
        if (text_output_path_flag) {
            text_output_path = args::get(text_output_path_flag);
            LOG(INFO) << "Output as text format: " << text_output_path;
        }
    }
};

void convert(std::vector<std::string> input_paths, std::string binary_output_path, std::string text_output_path) {
    std::vector<Edge> edges;
    for (auto path : input_paths) {
        FILE *f = fopen(path.c_str(), "r");
        CHECK(f != NULL);
        vertex_id_t src, degree;
        while (fscanf(f, "%u %u", &src, &degree) == 2) {
            vertex_id_t dst;
            for (vertex_id_t e_i = 0; e_i < degree; e_i++) {
                CHECK(1 == fscanf(f, "%u", &dst));
                edges.push_back(Edge(src, dst));
            }
        }
        fclose(f);
    }

    std::stringstream ss;
    ss << "# Converted from:";
    for (auto path : input_paths) {
        ss << " " << path;
    }
    ss << std::endl;
    ss << "# Edges: " << edges.size() << std::endl;
    std::string output_str = ss.str();

    if (binary_output_path.size() != 0) {
        std::stringstream oss;
        oss << output_str;
        write_binary_graph(binary_output_path.c_str(), edges, oss);
    }
    if (text_output_path.size() != 0) {
        std::stringstream oss;
        oss << output_str;
        write_text_graph(text_output_path.c_str(), edges, oss);
    }
}

int main(int argc, char** argv)
{
    init_glog(argv, google::INFO);

    FormatYahooOptionHelper opt;
    opt.parse(argc, argv);

    convert(opt.input_paths, opt.binary_output_path, opt.text_output_path);
    return 0;
}
