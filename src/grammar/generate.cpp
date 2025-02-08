#include <jinja2cpp/binding/nlohmann_json.h>
#include <jinja2cpp/template.h>
#include <nlohmann/json.hpp>

import std;

auto main(int argc, char * argv[]) -> int
{
    if (argc != 4) {
        std::println(std::cerr, "Usage: grammar_generator <grammar_json> <template> <output>");
        std::exit(EXIT_FAILURE);
    }

    std::ifstream grammar_file(argv[1]);
    nlohmann::json grammar = nlohmann::json::parse(grammar_file);
    grammar_file.close();

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
