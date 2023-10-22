
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "RMLite.h"

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

    // Handle Flag '-m'
    if (flag == "-m") {
      if (i + 1 < argc) {
        std::string value = argv[++i];
        flags.mappingFile = value;
      } else {
        std::cerr << flag << " requires an argument.\n";
      }
    }

    // Handle Flag '-o'
    else if (flag == "-o") {
      if (i + 1 < argc) {
        std::string value = argv[++i];
        flags.outputFile = value;
      } else {
        std::cerr << flag << " requires an argument.\n";
      }
    }

    // Handle Flag '-s'
    else if (flag == "-s") {
      flags.streamToFile = true;
    }

    else if (flag == "-d") {
      flags.checkDuplicates = true;
    }

    else {
      std::cerr << "Unknown flag: " << flag << "\n";
    }
  }

  // Error handling flags
  if (flags.mappingFile.empty()) {
    std::cerr << "Missing mandatory -m flag with mapping file path.\n";
    std::cerr << "Usage: " << argv[0] << " -m 'RML_FILE_PATH' [-o 'OUTPUT_FILE_PATH' -s \"USE STREAMING\" -d \"REMOVE DUPLICATES\"]\n";
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
  if (flags.streamToFile) {
    // Open a file in write mode
    std::ofstream outFile(flags.outputFile);

    // Check if the file is opened successfully
    if (!outFile.is_open()) {
      std::cerr << "Failed to open the output file!" << std::endl;
      return 1;
    }

    // Performe data mapping
    map_data_to_file(rml_rule, outFile, flags.checkDuplicates);

    // Close the file
    outFile.close();
  }
  ////// In Memory Mode: Store mapped data in memory //////
  else {
    std::unordered_set<NQuad> generated_quads = map_data(rml_rule, "");

    // Open a file in write mode
    std::ofstream outFile(flags.outputFile);

    // Check if the file is opened successfully
    if (!outFile.is_open()) {
      std::cerr << "Failed to open the output file!" << std::endl;
      return 1;
    }

    // Write to the file
    for (const NQuad& quad : generated_quads) {
      outFile << quad.subject.c_str() << " "
              << quad.predicate.c_str() << " "
              << quad.object.c_str() << " ";

      if (quad.graph != "") {
        outFile << quad.graph.c_str() << " .\n";
      } else {
        outFile << ".\n";
      }
    }

    // Close the file
    outFile.close();
  }

  // Stop timing
  auto end = std::chrono::high_resolution_clock::now();

  // Calculate the duration
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

  std::cout << "Mapping took " << duration << " milliseconds to execute." << std::endl;

  // std::cout << "Number of Allocations: " << s_AllocCount << std::endl;

  return 0;
}