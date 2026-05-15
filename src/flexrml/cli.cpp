#include "cli.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "version.h"

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
      options.mapping_source = require_value(arg);
    } else if (arg == "-o" || arg == "--output") {
      options.output_file_path = require_value(arg);
    } else if (arg == "-b" || arg == "--base") {
      options.base_uri = require_value(arg);
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
                   "  -o, --output OUTPUT         Write generated triples to a file\n"
                   "  -b, --base BASE             Default base IRI\n"
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
    throw std::runtime_error("No mapping specified. Use -m MAPPING.");
  }
  return options;
}
