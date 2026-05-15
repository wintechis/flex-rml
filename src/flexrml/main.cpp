#include <exception>
#include <iostream>

#include "cli.h"
#include "pipeline.h"

int main(int argc, char** argv) {
  try {
    auto options = parse_args(argc, argv);
    if (options.generate_plan) {
      std::cout << generate_plan(options) << "\n";
      return 0;
    }
    auto output = execute_mapping(options);
    if (!output.empty()) {
      std::cout << output << "\n";
    }
    return 0;
  } catch (const std::exception& exc) {
    std::cerr << "Error: " << exc.what() << "\n";
    return 1;
  }
}
