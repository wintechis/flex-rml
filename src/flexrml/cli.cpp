#include "cli.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sstream>

#include "version.h"

namespace {

std::string read_text_file(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Could not open --source file: " + path);
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void add_in_memory_source(Options& options, const std::string& spec) {
  const auto pos = spec.find('=');
  if (pos == std::string::npos || pos == 0) {
    throw std::runtime_error("--source expects name=value or name=@path");
  }

  const auto name = spec.substr(0, pos);
  const auto value = spec.substr(pos + 1);
  if (value.rfind("@", 0) == 0) {
    if (value.size() == 1) {
      throw std::runtime_error("--source file path is empty for source: " + name);
    }
    options.in_memory_sources[name] = read_text_file(value.substr(1));
  } else {
    options.in_memory_sources[name] = value;
  }
}

}  // namespace

Options parse_args(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto require_value = [&](const std::string& flag) -> std::string {
      if (i + 1 >= argc) {
        throw std::runtime_error("Missing value for " + flag);
      }
      return argv[++i];
    };

    if (arg == "-m" || arg == "--mapping") {
      if (options.mapping_source_is_string || !options.mapping_source.empty()) {
        throw std::runtime_error("Specify exactly one mapping input.");
      }
      options.mapping_source = require_value(arg);
    } else if (arg == "--mapping-string") {
      if (options.mapping_source_is_string || !options.mapping_source.empty()) {
        throw std::runtime_error("Specify exactly one mapping input.");
      }
      options.mapping_source = require_value(arg);
      options.mapping_source_is_string = true;
    } else if (arg == "-o" || arg == "--output") {
      options.output_file_path = require_value(arg);
    } else if (arg == "-b" || arg == "--base") {
      options.base_uri = require_value(arg);
    } else if (arg == "--source") {
      add_in_memory_source(options, require_value(arg));
    } else if (arg == "-gp" || arg == "--generate-plan") {
      options.generate_plan = true;
    } else if (arg == "--no-threading") {
      options.threading_enabled = "false";
    } else if (arg == "--no-const-folding") {
      options.materialize_constants = "false";
    } else if (arg == "--no-ordering") {
      options.heuristic_ordering = "false";
    } else if (arg == "-v" ||arg == "--version") {
      std::cout << "flexrml " << FLEXRML_VERSION << "\n";
      std::exit(0);
    } else if (arg == "-h" || arg == "--help") {
      std::cout << "usage: flexrml -m MAPPING [options]\n"
                   "       flexrml --version\n"
                   "\n"
                   "options:\n"
                   "  -m, --mapping MAPPING       Mapping file path or mapping string\n"
                   "      --mapping-string TEXT   Mapping string literal\n"
                   "  -o, --output OUTPUT         Write generated triples to a file\n"
                   "  -b, --base BASE             Default base IRI\n"
                   "      --source NAME=VALUE     Bind an in-memory source string\n"
                   "      --source NAME=@PATH     Bind an in-memory source from a file\n"
                   "  -gp, --generate-plan        Print the generated execution plan\n"
                   "      --no-threading          Run without worker threads\n"
                   "      --no-const-folding      Disable constant folding in planning\n"
                   "      --no-ordering           Disable heuristic plan ordering\n"
                   "  -h, --help                  Show this help text\n"
                   "  -v, --version               Show version information\n";
      std::exit(0);
    } else {
      throw std::runtime_error("Unknown argument: " + arg);
    }
  }
  if (options.mapping_source.empty()) {
    throw std::runtime_error("No mapping specified. Use -m MAPPING or --mapping-string TEXT.");
  }
  return options;
}
