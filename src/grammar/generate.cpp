#include <jinja2cpp/template.h>
#include <yaml-cpp/yaml.h>

import jinja2_yaml_binding;

import std;

auto main(int argc, char * argv[]) -> int
{
    if (argc != 4) {
        std::println(std::cerr, "Usage: grammar_generator <grammar_yaml> <template> <output>");
        std::exit(EXIT_FAILURE);
    }

    YAML::Node grammar = YAML::LoadFile(argv[1]);

    std::ifstream template_file(argv[2]);
    std::stringstream buffer;
    buffer << template_file.rdbuf();
    template_file.close();

    jinja2::Template tmpl;

    std::ignore = tmpl.Load(buffer.str());
    jinja2::ValuesMap params{{"data", jinja2::Reflect(grammar)}};

    std::ofstream output(argv[3]);
    std::ignore = tmpl.Render(output, params);
    output.close();
}
