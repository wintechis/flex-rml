
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

bool handle_flags(const int& argc, char* argv[], Flags& flags) {
  for (int i = 1; i < argc; i++) {
    std::string flag = argv[i];

    // Handle config file
    if (flag == "-c") {
      if (i + 1 < argc) {
        std::string config_file_path = argv[++i];
        std::ifstream config_file(config_file_path);
        std::string line;

        if (config_file.is_open()) {
          while (getline(config_file, line)) {
            // Ignore comments
            if (line.rfind("//", 0) == 0) {
              continue;
            }

            std::istringstream iss(line);
            std::string key, value;
            if (getline(iss, key, '=') && getline(iss, value)) {
              if (key == "mapping") {
                flags.mappingFile = value;
              } else if (key == "output_file") {
                flags.outputFile = value;
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
                std::string value = argv[++i];
                try {
                  flags.thread_count = std::stoi(value);
                } catch (const std::invalid_argument& e) {
                  throw_error("Invalid thread count! - Only integers are allowed.");
                } catch (const std::out_of_range& e) {
                  throw_error("Invalid thread count! - The number is out of range for an integer.");
                }
              } else {
                std::cerr << "Unknown flag: " << flag << "\n";
              }
            }
            config_file.close();
          }
        } else {
          std::cerr << "Unable to open config file: " << config_file_path << "\n";
          return false;
        }
      } else {
        std::cerr << flag << " requires an argument.\n";
        return false;
      }
    }

    // -m Path to mapping file
    if (flag == "-m") {
      if (i + 1 < argc) {
        std::string value = argv[++i];
        flags.mappingFile = value;
      } else {
        std::cerr << flag << " requires an argument.\n";
      }
    }

    // -o Path to output file
    else if (flag == "-o") {
      if (i + 1 < argc) {
        std::string value = argv[++i];
        flags.outputFile = value;
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
      std::cerr << "Unknown flag: " << flag << "\n";
    }
  }

  // Error handling flags
  if (flags.mappingFile.empty()) {
    std::cerr << "Missing mandatory -m flag with mapping file path.\n";
    std::cerr << "Usage: " << argv[0] << " -m 'RML_FILE_PATH' [-o 'OUTPUT_FILE_PATH' -d \"REMOVE DUPLICATES\" -a \"ADAPTIVE SELECTION OF HASH SIZE\" -t \"USE THREADING\" -tc \"NUMBER OF THREADS TO USE\" -p \"SAMPLING PROBABILITY TO USE\"]\n";
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
  if (!readRmlFile(flags.mappingFile, rml_rule)) {
    return 1;
  }

  // Start timing
  auto start = std::chrono::high_resolution_clock::now();

  /////////////////////////////////
  //////// Prepare Mapping ////////
  /////////////////////////////////

  ////// Streaming Mode: Stream data to file //////
  // Open a file in write mode
  std::ofstream outFile(flags.outputFile);

  // Check if the file is opened successfully
  if (!outFile.is_open()) {
    std::cerr << "Failed to open the output file!" << std::endl;
    return 1;
  }

  if (flags.threading) {
    // Performe data mapping using threads
    map_data_to_file_threading(rml_rule, outFile, flags.check_duplicates, flags.adaptive_hash_selection, flags.thread_count, flags.sampling_probability);
  } else {
    // Performe data mapping
    map_data_to_file(rml_rule, outFile, flags.check_duplicates, flags.adaptive_hash_selection, flags.sampling_probability);
  }

  // Close the file
  outFile.close();

  // Stop timing
  auto end = std::chrono::high_resolution_clock::now();

  // Calculate the duration
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

  std::cout << "Mapping took " << duration << " milliseconds to execute." << std::endl;

  // std::cout << "Number of Allocations: " << s_AllocCount << std::endl;

  return 0;
}