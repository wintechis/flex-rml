
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "FlexRML.h"

/*
static uint32_t s_AllocCount = 0;

void* operator new(size_t size) {
  s_AllocCount++;
  return malloc(size);
} */

bool readRmlFile(const std::string& filePath, std::string& rml_rule) {
  // Open the rml file in input mode
  std::ifstream file(filePath);

  // Check if the file was opened successfully
  if (!file.is_open()) {
    std::cerr << "Could not open RML file!" << std::endl;
    return false;
  }

  // Read the whole file into a std::string
  std::ostringstream ss;
  ss << file.rdbuf();
  rml_rule = ss.str();

  // Close the file
  file.close();

  return true;
}

/**
 * Splits a string into a set based on a specified delimiter.
 *
 * @param str The string to be split.
 * @param delimiter The character used as a delimiter to split the string.
 * @return std::unordered_set<std::string> A set containing the split substrings.
 */
std::unordered_set<std::string> split_to_set(const std::string& str, char delimiter) {
  std::unordered_set<std::string> result_set;
  std::string::size_type start = 0;
  std::string::size_type end = str.find(delimiter);

  while (end != std::string::npos) {
    result_set.insert(str.substr(start, end - start));
    start = end + 1;
    end = str.find(delimiter, start);
  }

  result_set.insert(str.substr(start));
  return result_set;
}

void display_help() {
  std::cout << "FlexRML is a flexible RML processor optimized for a wide range of devices.\n"
            << "Usage: ./FlexRML [OPTIONS]\n"
            << "-m [path] Specify the path to the mapping file.\n"
            << "-o [name] Define the name for the output file. Default is 'output.nq' \n"
            << "-d Remove duplicate entries before writing to the output file.\n"
            << "-t Use threading, by default the maximum number of available threads are used.\n"
            << "-tc [integer] Specify the number of threads that should be used.\n"
            << "-a Use adaptive Result Size Estimation and adaptive hash size selection.\n"
            << "-p [float] Set sampling probability used for Result Size Estimation. Higher probabities produce better estimates but need more time.\n"
            << "-c [path] Use config file instead of command line arguments.\n"
            << "-b [integer] Use a fixed hash size, value which must be one of [32, 64, 128]\n"
            << "\n"
            << "Notes:\n"
            << "When a config file is specified using the '-c' flag, all other command-line arguments are ignored, and settings are exclusively loaded from the config file.\n"
            << "Selecting a fixed hash size using the '-b' flag skips the adaptive Result Size Estimation. Be aware that if the manually chosen hash size is too small for the input data, hash collisions may occur. This can lead to missing N-Quads in the output.\n"
            << "\n"
            << "Configuration for fastest mapping:\n"
            << "./FlexRML -m [path] -d -t\n"
            << "Configuration for least memory usage during mapping:\n"
            << "./FlexRML -m [path] -d -t -a\n"
            << std::endl;
}

bool handle_flags(const int& argc, char* argv[], Flags& flags) {
  for (int i = 1; i < argc; i++) {
    std::string flag = argv[i];

    if (flag == "-h") {
      display_help();
      std::exit(0);
    }

    // Handle config file
    if (flag == "-c") {
      if (i + 1 < argc) {
        std::string config_file_path = argv[++i];
        std::ifstream config_file(config_file_path);
        std::string line;

        if (config_file.is_open()) {
          while (getline(config_file, line)) {
            // Ignore comments and empty lines
            if (line.empty() || line.rfind("#", 0) == 0) {
              continue;
            }

            std::istringstream iss(line);
            std::string key, value;
            if (getline(iss, key, '=') && getline(iss, value)) {
              if (key == "mapping") {
                flags.mapping_file = value;
              } else if (key == "output_file") {
                flags.output_file = value;
              } else if (key == "remove_duplicates") {
                flags.check_duplicates = (value == "true");
              } else if (key == "use_threading") {
                flags.threading = (value == "true");
              } else if (key == "adaptive_hash_selection") {
                flags.adaptive_hash_selection = (value == "true");
              } else if (key == "sampling_probability") {
                try {
                  float parsed_value = std::stof(value);

                  // Check if the value is within the specified range
                  if (parsed_value <= 0.0f || parsed_value >= 1.0f) {
                    throw_error("Invalid sampling probability! - Only values in the range 0 < sampling_probability < 1 are allowed.");
                  }

                  flags.sampling_probability = parsed_value;
                } catch (const std::invalid_argument& e) {
                  throw_error("Invalid sampling probability! - The provided value is not a valid floating-point number.");
                } catch (const std::out_of_range& e) {
                  throw_error("Invalid sampling probability! - The number is out of range for a float.");
                }
              } else if (key == "number_of_threads") {
                try {
                  flags.thread_count = std::stoi(value);
                } catch (const std::invalid_argument& e) {
                  throw_error("Invalid thread count! - Only integers are allowed.");
                } catch (const std::out_of_range& e) {
                  throw_error("Invalid thread count! - The number is out of range for an integer.");
                }
              } else if (key == "fixed_bit_size") {
                try {
                  int parsed_value = std::stoi(value);

                  int allowed_values[] = {32, 64, 128};

                  // Check if the value is within allowed values
                  bool is_allowed = std::find(std::begin(allowed_values), std::end(allowed_values), parsed_value) != std::end(allowed_values);
                  if (!is_allowed) {
                    throw_error("Invalid bit size! - Only values 32, 64, 128 are allowed.");
                  }

                  flags.fixed_bit_size = parsed_value;
                } catch (const std::invalid_argument& e) {
                  throw_error("Invalid bit size! - Only integers 32, 64, 128 are allowed.");
                } catch (const std::out_of_range& e) {
                  throw_error("Invalid bit size! - The number is out of range for an integer.");
                }
              } else if (key == "tokens_to_remove") {
                std::unordered_set<std::string> tokens_set = split_to_set(value, ',');
                flags.tokens_to_remove = tokens_set;
              } else {
                std::cerr << "Unknown flag: " << flag << "\n";
              }
            }
          }
          config_file.close();

        } else {
          std::cerr << "Unable to open config file: " << config_file_path << "\n";
          return false;
        }
      } else {
        std::cerr << flag << " requires an argument.\n";
        return false;
      }
      return true;
    }

    // -m Path to mapping file
    if (flag == "-m") {
      if (i + 1 < argc) {
        std::string value = argv[++i];
        flags.mapping_file = value;
      } else {
        std::cerr << flag << " requires an argument.\n";
      }
    }

    // -o Path to output file
    else if (flag == "-o") {
      if (i + 1 < argc) {
        std::string value = argv[++i];
        flags.output_file = value;
      } else {
        std::cerr << flag << " requires an argument.\n";
      }
    }

    // Remove duplicates
    else if (flag == "-d") {
      flags.check_duplicates = true;
    }

    // Use multithreading
    else if (flag == "-t") {
      flags.threading = true;
    }

    // Use adaptive hash selection
    else if (flag == "-a") {
      flags.adaptive_hash_selection = true;
    }

    // List of elements to remove
    else if (flag == "-r") {
      if (i + 1 < argc) {
        std::string value = argv[++i];

        std::unordered_set<std::string> tokens_set = split_to_set(value, ',');
        flags.tokens_to_remove = tokens_set;

      } else {
        std::cerr << flag << " requires an argument.\n";
      }
    }

    // Set sampling probability
    else if (flag == "-p") {
      if (i + 1 < argc) {
        std::string value = argv[++i];
        try {
          float parsed_value = std::stof(value);

          // Check if the value is within the specified range
          if (parsed_value <= 0.0f || parsed_value >= 1.0f) {
            throw_error("Invalid sampling probability! - Only values in the range 0 < sampling_probability < 1 are allowed.");
          }

          flags.sampling_probability = parsed_value;
        } catch (const std::invalid_argument& e) {
          throw_error("Invalid sampling probability! - The provided value is not a valid floating-point number.");
        } catch (const std::out_of_range& e) {
          throw_error("Invalid sampling probability! - The number is out of range for a float.");
        }
      } else {
        std::cerr << flag << " requires an argument.\n";
      }
    }

    // Set bitsize of hash function
    else if (flag == "-b") {
      if (i + 1 < argc) {
        std::string value = argv[++i];
        try {
          int parsed_value = std::stoi(value);

          int allowed_values[] = {32, 64, 128};

          // Check if the value is within allowed values
          bool is_allowed = std::find(std::begin(allowed_values), std::end(allowed_values), parsed_value) != std::end(allowed_values);
          if (!is_allowed) {
            throw_error("Invalid bit size! - Only values 32, 64, 128 are allowed.");
          }

          flags.fixed_bit_size = parsed_value;
        } catch (const std::invalid_argument& e) {
          throw_error("Invalid bit size! - Only integers 32, 64, 128 are allowed.");
        } catch (const std::out_of_range& e) {
          throw_error("Invalid bit size! - The number is out of range for an integer.");
        }
      } else {
        std::cerr << flag << " requires an argument.\n";
      }
    }

    // Set number of threads to use
    else if (flag == "-tc") {
      if (i + 1 < argc) {
        std::string value = argv[++i];
        try {
          flags.thread_count = std::stoi(value);
        } catch (const std::invalid_argument& e) {
          throw_error("Invalid thread count! - Only integers are allowed.");
        } catch (const std::out_of_range& e) {
          throw_error("Invalid thread count! - The number is out of range for an integer.");
        }
      } else {
        std::cerr << flag << " requires an argument.\n";
      }
    }

    else {
      std::cerr << "Unknown command line flag: " << flag << "\n";
    }
  }

  // Error handling flags
  if (flags.mapping_file.empty()) {
    std::cerr << "Missing mandatory -m flag with mapping file path.\n";
    std::cerr << "Usage: " << argv[0] << " -m 'RML_FILE_PATH' [-o 'OUTPUT_FILE_PATH' -d \"REMOVE DUPLICATES\" -a \"ADAPTIVE SELECTION OF HASH SIZE\" -t \"USE THREADING\" -tc \"NUMBER OF THREADS\" -p \"SAMPLING PROBABILITY\" -c \"PATH TO CONFIG FILE\" -b \"FIXED BIT SIZE\"] -r \"ELEMENTS,TO,REMOVE\"]\n";
    return false;
  }
  return true;
}

int main(int argc, char* argv[]) {
  // Extract command line arguments
  Flags flags;
  if (!handle_flags(argc, argv, flags)) {
    return 1;
  };

  // Load RML rule
  std::string rml_rule;
  if (!readRmlFile(flags.mapping_file, rml_rule)) {
    return 1;
  }

  // Start timing
  auto start = std::chrono::high_resolution_clock::now();

  std::cout << "Processing: " << flags.mapping_file << std::endl;
  std::cout << "Output file: " << flags.output_file << std::endl;

  /////////////////////////////////
  //////// Prepare Mapping ////////
  /////////////////////////////////

  ////// Streaming Mode: Stream data to file //////
  // Open a file in write mode
  std::ofstream out_file(flags.output_file);

  // Check if the file is opened successfully
  if (!out_file.is_open()) {
    std::cerr << "Failed to open the output file!" << std::endl;
    return 1;
  }

  if (flags.threading) {
    // Performe data mapping using threads
    map_data_to_file_threading(rml_rule, out_file, flags);
  } else {
    // Performe data mapping
    map_data_to_file(rml_rule, out_file, flags);
  }

  // Close the file
  out_file.close();

  // Stop timing
  auto end = std::chrono::high_resolution_clock::now();

  // Calculate the duration
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

  std::cout << "Mapping took " << duration << " milliseconds to execute." << std::endl;

  // std::cout << "Number of Allocations: " << s_AllocCount << std::endl;

  return 0;
}