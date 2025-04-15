import sys

from jinja2 import Environment, FileSystemLoader, select_autoescape
import yaml

def main():
    if len(sys.argv) != 4:
        print("Usage: generate.py <grammar_yaml> <template> <output>", file=sys.stderr)
        sys.exit(1)
    
    with open(sys.argv[1], 'r') as grammar_file:
        grammar = yaml.safe_load(grammar_file)

    jinja_env = Environment(loader=FileSystemLoader('.'), autoescape=select_autoescape())
    template = jinja_env.get_template(sys.argv[2])

    with open(sys.argv[3], 'w') as output_file:
        output_file.write(template.render(data=grammar))

if __name__ == "__main__":
    main()
