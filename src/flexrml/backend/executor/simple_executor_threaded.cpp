#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "definitions.h"
#include "utils.h"

namespace fs = std::filesystem;

//////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Thread-safe Bounded Queue (template)
//////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename T>
class BoundedThreadSafeQueue {
 public:
  explicit BoundedThreadSafeQueue(size_t capacity) : capacity_(capacity) {}

  void push(T value) {
    std::unique_lock<std::mutex> lock(mtx_);
    cond_push_.wait(lock,
                    [this] { return queue_.size() < capacity_ || finished_; });
    if (finished_) return;
    queue_.push(std::move(value));
    cond_pop_.notify_one();
  }

  bool pop(T& value) {
    std::unique_lock<std::mutex> lock(mtx_);
    cond_pop_.wait(lock, [this] { return !queue_.empty() || finished_; });
    if (queue_.empty()) return false;
    value = std::move(queue_.front());
    queue_.pop();
    cond_push_.notify_one();
    return true;
  }

  void set_finished() {
    std::lock_guard<std::mutex> lock(mtx_);
    finished_ = true;
    cond_pop_.notify_all();
    cond_push_.notify_all();
  }

  // Add this method to get the current size safely
  size_t size() {
    std::lock_guard<std::mutex> lock(mtx_);
    return queue_.size();
  }

 private:
  std::queue<T> queue_;
  std::mutex mtx_;
  std::condition_variable cond_pop_, cond_push_;
  bool finished_ = false;
  size_t capacity_;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handle_constant_preformatted(const std::vector<std::string>& s_content, const std::vector<std::string>& p_content,
                                  const std::vector<std::string>& o_content, const std::vector<std::string>& g_content,
                                  const fs::path& output_file_name, std::unordered_set<uint64_t>& global_hashes) {
  std::ofstream outputFile(output_file_name, std::ios::app);
  if (!outputFile) {
    std::cout << "Error: Unable to open file for writing." << std::endl;
    std::exit(1);
  }
  std::vector<std::string> val;
  if (g_content.empty()) {
    val = {s_content[0], p_content[0], o_content[0]};
  } else {
    val = {s_content[0], p_content[0], o_content[0], g_content[0]};
  }

  uint64_t rowHash = combinedHash(val);
  if (global_hashes.find(rowHash) != global_hashes.end()) {
    // skip
    return;
  }
  global_hashes.insert(rowHash);

  for (const auto& element : val) {
    outputFile << element + " ";
  }
  outputFile << ".\n";
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Main Execution Dependent
//////////////////////////////////////////////////////////////////////////////////////////////////////////

// store chunk as vector of strings
using CSVChunk = std::vector<std::vector<std::string>>;
// producers process triple strings in chunks
using TripleChunk = std::vector<std::string>;

std::unordered_set<uint64_t> execute_dependent(
    const std::string& input_file_name, const fs::path& output_file_name,
    const std::string& base_uri,
    const std::vector<std::string>& projected_attributes,
    // s_content = e.g. {"ex:Person/{id}", "template", "iri", "", ""}
    const std::vector<std::string>& s_content,
    const std::vector<std::string>& p_content,
    const std::vector<std::string>& o_content,
    std::unordered_set<uint64_t>& global_hashes) {
  // --------------------------------------
  if (projected_attributes.size() == 1 && projected_attributes[0] == "") {
    // Must be constant if no attriutes are projected
    const std::vector<std::string> g_content;
    handle_constant_preformatted(s_content, p_content, o_content, g_content, output_file_name, global_hashes);
    return global_hashes;
  }

  // Open file and read header
  std::ifstream file(input_file_name);
  if (!file.is_open()) {
    std::cerr << "Error opening file: " << input_file_name << "\n";
    std::exit(1);
  }

  std::string headerLine;
  if (!std::getline(file, headerLine)) {
    std::cerr << "CSV file is empty or missing header\n";
    std::exit(1);
  }
  std::vector<std::string> header = split_csv_line(headerLine, ',');

  // find indexes in header for projected attributes
  std::vector<int> projected_indexes;
  projected_indexes.reserve(projected_attributes.size());
  for (const auto& attr : projected_attributes) {
    int idx = get_index(header, attr);
    projected_indexes.push_back(idx);
  }

  // keep a copy of projected attribute names
  std::vector<std::string> projected_header = projected_attributes;

  // --------------------------------------
  // thread-safe queues
  const int chunks_to_buffer = 10;
  const size_t QUEUE_CAPACITY = chunks_to_buffer;
  BoundedThreadSafeQueue<CSVChunk> lineQueue(QUEUE_CAPACITY);

  const size_t TRIPLE_QUEUE_CAPACITY = chunks_to_buffer;
  BoundedThreadSafeQueue<TripleChunk> tripleQueue(TRIPLE_QUEUE_CAPACITY);

  // --------------------------------------
  // setup Reader thread: read lines in chunks + deduplicate
  const size_t CHUNK_SIZE = 200;  // lines per chunk
  std::thread reader([&]() {
    // std::unordered_set<uint64_t> global_hashes;
    // global_hashes.reserve(1024 * 1024);

    CSVChunk chunk;
    chunk.reserve(CHUNK_SIZE);

    std::string line;
    while (std::getline(file, line)) {
      // Parse and get projected fields
      auto split_line = split_csv_line(line, ',');

      std::vector<std::string> projectedFields;
      projectedFields.reserve(projected_indexes.size());
      for (int idx : projected_indexes) {
        if (idx >= 0 && idx < (int)split_line.size()) {
          projectedFields.push_back(split_line[idx]);
        } else {
          projectedFields.push_back("");
        }
      }

      // Deduplicate
      // uint64_t rowHash = combinedHash(projectedFields);
      // if (global_hashes.find(rowHash) != global_hashes.end()) {
      // skip
      // continue;
      //}
      // global_hashes.insert(rowHash);

      // Add to chunk
      chunk.push_back(std::move(projectedFields));
      if (chunk.size() >= CHUNK_SIZE) {
        lineQueue.push(std::move(chunk));
        chunk.clear();
        chunk.reserve(CHUNK_SIZE);
      }
    }
    // push last partial chunk if not empty
    if (!chunk.empty()) {
      lineQueue.push(std::move(chunk));
    }
    file.close();
    lineQueue.set_finished();
  });

  // --------------------------------------
  // Producer threads: transform chunk of CSV -> chunk of triple strings
  unsigned int NUM_PRODUCERS = std::thread::hardware_concurrency();
  if (NUM_PRODUCERS == 0) {
    NUM_PRODUCERS = 2;  // fallback
  }

  std::vector<std::thread> producers;
  producers.reserve(NUM_PRODUCERS);

  for (unsigned int i = 0; i < NUM_PRODUCERS; i++) {
    producers.emplace_back([&]() {
      CSVChunk csvChunk;
      while (lineQueue.pop(csvChunk)) {
        // Build a TripleChunk
        TripleChunk tripleChunk;
        tripleChunk.reserve(csvChunk.size());

        for (auto& rowFields : csvChunk) {
          // create a map from projected_header
          std::unordered_map<std::string, std::string> rowMap;
          for (size_t j = 0; j < rowFields.size(); j++) {
            rowMap[projected_header[j]] = rowFields[j];
          }

          ////// CREATE //////
          std::string subject;
          std::string predicate;
          std::string object;

          try {
            // SUBJECT
            if (s_content[1] == "preformatted") {
              subject = s_content[0];
            } else {
              subject = create_operator(s_content[0], s_content[1], s_content[2], "", "", base_uri, rowMap);
            }
            // PREDICATE
            if (p_content[1] == "preformatted") {
              predicate = p_content[0];
            } else {
              predicate =
                  create_operator(p_content[0], p_content[1], p_content[2], "", "", base_uri, rowMap);
            }
            // OBJECT
            if (o_content[1] == "preformatted") {
              object = o_content[0];
            } else {
              object =
                  create_operator(o_content[0], o_content[1], o_content[2], o_content[3], o_content[4], base_uri, rowMap);
            }
          } catch (const std::runtime_error& e) {
            if (continue_on_error == false) {
              std::cout << e.what() << std::endl;
              std::exit(1);
            } else {
              continue;
            }
          }

          std::string tripleStr = subject + " " + predicate + " " + object + " .\n";
          tripleChunk.push_back(std::move(tripleStr));
        }

        // push the tripleChunk
        tripleQueue.push(std::move(tripleChunk));
      }
    });
  }

  // --------------------------------------
  // Consumer thread: write triple chunks to file
  std::thread consumer([&]() {
    // pop entire TripleChunks and write them in one go
    TripleChunk tripleChunk;
    std::ofstream outputFile(output_file_name, std::ios::app);
    if (!outputFile) {
      std::cout << "Error: Unable to open file for writing." << std::endl;
      std::exit(1);
    }
    size_t counter = 0;
    std::string buffer = "";
    buffer.reserve(1024);
    while (tripleQueue.pop(tripleChunk)) {
      // Write everything
      for (auto& t : tripleChunk) {
        // Deduplicate
        std::vector<std::string> val = {t};
        uint64_t rowHash = combinedHash(val);
        if (global_hashes.find(rowHash) != global_hashes.end()) {
          // skip
          continue;
        }
        global_hashes.insert(rowHash);
        buffer += t;
        counter++;

        if (counter == 50) {
          outputFile << buffer;
          buffer = "";
          counter = 0;
        }
      }
    }
    // Flush any remaining content in the buffer
    if (!buffer.empty()) {
      outputFile << buffer;
    }
    outputFile.close();
  });

  // --------------------------------------
  // Wait for everything to finish
  reader.join();
  for (auto& th : producers) {
    th.join();
  }

  tripleQueue.set_finished();
  consumer.join();

  return global_hashes;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Main Execution
//////////////////////////////////////////////////////////////////////////////////////////////////////////

size_t execute(const std::string& input_file_name,
               const fs::path& output_file_name, const std::string& base_uri,
               const std::vector<std::string>& projected_attributes,
               // s_content = e.g. {"ex:Person/{id}", "template", "iri", "", ""}
               const std::vector<std::string>& s_content,
               const std::vector<std::string>& p_content,
               const std::vector<std::string>& o_content) {
  if (projected_attributes.size() == 1 && projected_attributes[0] == "") {
    // Must be constant if no attriutes are projected
    const std::vector<std::string> g_content;
    std::unordered_set<uint64_t> global_hashes;
    handle_constant_preformatted(s_content, p_content, o_content, g_content, output_file_name, global_hashes);
    return 1;
  }

  std::atomic<size_t> triple_count(0);

  // --------------------------------------
  // Open file and read header
  std::ifstream file(input_file_name);
  if (!file.is_open()) {
    std::cerr << "Error opening file: " << input_file_name << "\n";
    return 1;
  }

  std::string headerLine;
  if (!std::getline(file, headerLine)) {
    std::cerr << "CSV file is empty or missing header\n";
    return 1;
  }
  std::vector<std::string> header = split_csv_line(headerLine, ',');

  // find indexes in header for projected attributes
  std::vector<int> projected_indexes;
  projected_indexes.reserve(projected_attributes.size());
  for (const auto& attr : projected_attributes) {
    int idx = get_index(header, attr);
    projected_indexes.push_back(idx);
  }

  // keep a copy of projected attribute names
  std::vector<std::string> projected_header = projected_attributes;

  // --------------------------------------
  // thread-safe queues
  const int chunks_to_buffer = 20;
  const size_t QUEUE_CAPACITY = chunks_to_buffer;
  BoundedThreadSafeQueue<CSVChunk> lineQueue(QUEUE_CAPACITY);

  const size_t TRIPLE_QUEUE_CAPACITY = chunks_to_buffer;
  BoundedThreadSafeQueue<TripleChunk> tripleQueue(TRIPLE_QUEUE_CAPACITY);

  // --------------------------------------
  // setup Reader thread: read lines in chunks + deduplicate
  const size_t CHUNK_SIZE = 50;  // lines per chunk
  std::thread reader([&]() {
    CSVChunk chunk;
    chunk.reserve(CHUNK_SIZE);

    std::string line;
    while (std::getline(file, line)) {
      // Parse and get projected fields
      auto split_line = split_csv_line(line, ',');

      std::vector<std::string> projectedFields;
      projectedFields.reserve(projected_indexes.size());
      for (int idx : projected_indexes) {
        if (idx >= 0 && idx < (int)split_line.size()) {
          projectedFields.push_back(split_line[idx]);
        } else {
          projectedFields.push_back("");
        }
      }

      // Add to chunk
      chunk.push_back(std::move(projectedFields));
      if (chunk.size() >= CHUNK_SIZE) {
        lineQueue.push(std::move(chunk));
        chunk.clear();
        chunk.reserve(CHUNK_SIZE);
      }
    }
    // push last partial chunk if not empty
    if (!chunk.empty()) {
      lineQueue.push(std::move(chunk));
    }
    file.close();
    lineQueue.set_finished();
  });

  // --------------------------------------
  // Producer threads: transform chunk of CSV -> chunk of triple strings
  unsigned int NUM_PRODUCERS = std::thread::hardware_concurrency();
  if (NUM_PRODUCERS == 0) {
    NUM_PRODUCERS = 2;  // fallback
  }

  std::vector<std::thread> producers;
  producers.reserve(NUM_PRODUCERS);

  for (unsigned int i = 0; i < NUM_PRODUCERS; i++) {
    producers.emplace_back([&]() {
      size_t local_triple_count = 0;  // Thread-local counter
      CSVChunk csvChunk;
      while (lineQueue.pop(csvChunk)) {
        // Log current queue size (this logs the lineQueue size in the producer
        // thread)
        // std::cout << "[Producer " << std::this_thread::get_id()
        //          << "] lineQueue size: " << lineQueue.size() << "\n";

        // Build a TripleChunk for the current CSV chunk.
        TripleChunk tripleChunk;
        tripleChunk.reserve(csvChunk.size());

        for (auto& rowFields : csvChunk) {
          // Create a map from projected_header.
          std::unordered_map<std::string, std::string> rowMap;
          for (size_t j = 0; j < rowFields.size(); j++) {
            rowMap[projected_header[j]] = rowFields[j];
          }

          // Check for NULL values
          bool skip = false;
          for (const auto& target : values_to_skip) {
            if (std::any_of(rowMap.begin(), rowMap.end(), [&target](const auto& pair) {
                  return pair.second == target;
                })) {
              skip = true;
              break;
            }
          }
          if (skip) {
            continue;
          }

          // Create subject, predicate, object.
          std::string subject, predicate, object;
          try {
            if (s_content[1] == "preformatted") {
              subject = s_content[0];
            } else {
              subject = create_operator(s_content[0], s_content[1], s_content[2], "", "", base_uri, rowMap);
            }
            if (p_content[1] == "preformatted") {
              predicate = p_content[0];
            } else {
              predicate = create_operator(p_content[0], p_content[1], p_content[2], "", "", base_uri, rowMap);
            }
            if (o_content[1] == "preformatted") {
              object = o_content[0];
            } else {
              object = create_operator(o_content[0], o_content[1], o_content[2], o_content[3], o_content[4], base_uri, rowMap);
            }
          } catch (const std::runtime_error& e) {
            if (!continue_on_error) {
              std::cout << e.what() << std::endl;
              std::exit(1);
            } else {
              continue;
            }
          }

          std::string tripleStr = subject + " " + predicate + " " + object + " .\n";
          tripleChunk.push_back(std::move(tripleStr));
        }

        // Accumulate count.
        local_triple_count += tripleChunk.size();

        // Push the chunk
        tripleQueue.push(std::move(tripleChunk));

        // Optionally, log tripleQueue size as well
        // std::cout << "[Producer " << std::this_thread::get_id()
        //          << "] tripleQueue size: " << tripleQueue.size() << "\n";

        // Update the global atomic counter
        triple_count.fetch_add(local_triple_count, std::memory_order_relaxed);
        local_triple_count = 0;
      }
      // Flush any remaining count
      if (local_triple_count > 0) {
        triple_count.fetch_add(local_triple_count, std::memory_order_relaxed);
      }
    });
  }
  // --------------------------------------
  // Consumer thread: write triple chunks to file
  std::thread consumer([&]() {
    fs::create_directories(output_file_name.parent_path());

    std::ofstream outputFile(output_file_name, std::ios::app);
    if (!outputFile) {
      std::cerr << "Cannot open output file: " << output_file_name << "\n";
      return;
    }
    std::unordered_set<uint64_t> global_hashes;
    global_hashes.reserve(1024 * 1024 * 2);
    // pop entire TripleChunks and write them in one go
    TripleChunk tripleChunk;
    size_t counter = 0;
    std::string buffer = "";
    buffer.reserve(1024 * 30);
    while (tripleQueue.pop(tripleChunk)) {
      // Write everything
      for (auto& t : tripleChunk) {
        // Deduplicate
        std::vector<std::string> val = {t};
        uint64_t rowHash = combinedHash(val);
        if (global_hashes.find(rowHash) != global_hashes.end()) {
          // skip
          continue;
        }
        global_hashes.insert(rowHash);
        buffer += t;
        counter++;

        if (counter == 500) {
          outputFile << buffer;
          buffer = "";
          counter = 0;
        }
      }
    }
    // Flush any remaining content in the buffer
    if (!buffer.empty()) {
      outputFile << buffer;
    }
    outputFile.close();
  });

  // --------------------------------------
  // Wait for everything to finish
  reader.join();
  for (auto& th : producers) {
    th.join();
  }

  tripleQueue.set_finished();
  consumer.join();

  return triple_count.load();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////

size_t execute_with_graph(const std::string& input_file_name,
                          const fs::path& output_file_name, const std::string& base_uri,
                          const std::vector<std::string>& projected_attributes,
                          // s_content = e.g. {"ex:Person/{id}", "template", "iri", "", ""}
                          const std::vector<std::string>& s_content,
                          const std::vector<std::string>& p_content,
                          const std::vector<std::string>& o_content,
                          const std::vector<std::string>& g_content) {
  if (projected_attributes.size() == 1 && projected_attributes[0] == "") {
    // Must be constant if no attriutes are projected
    std::unordered_set<uint64_t> global_hashes;
    handle_constant_preformatted(s_content, p_content, o_content, g_content, output_file_name, global_hashes);
    return 1;
  }

  std::atomic<size_t> triple_count(0);

  // --------------------------------------
  // Open file and read header
  std::ifstream file(input_file_name);
  if (!file.is_open()) {
    std::cerr << "Error opening file: " << input_file_name << "\n";
    return 1;
  }

  std::string headerLine;
  if (!std::getline(file, headerLine)) {
    std::cerr << "CSV file is empty or missing header\n";
    return 1;
  }
  std::vector<std::string> header = split_csv_line(headerLine, ',');

  // find indexes in header for projected attributes
  std::vector<int> projected_indexes;
  projected_indexes.reserve(projected_attributes.size());
  for (const auto& attr : projected_attributes) {
    int idx = get_index(header, attr);
    projected_indexes.push_back(idx);
  }

  // keep a copy of projected attribute names
  std::vector<std::string> projected_header = projected_attributes;

  // --------------------------------------
  // thread-safe queues
  const int chunks_to_buffer = 20;
  const size_t QUEUE_CAPACITY = chunks_to_buffer;
  BoundedThreadSafeQueue<CSVChunk> lineQueue(QUEUE_CAPACITY);

  const size_t TRIPLE_QUEUE_CAPACITY = chunks_to_buffer;
  BoundedThreadSafeQueue<TripleChunk> tripleQueue(TRIPLE_QUEUE_CAPACITY);

  // --------------------------------------
  // setup Reader thread: read lines in chunks + deduplicate
  const size_t CHUNK_SIZE = 50;  // lines per chunk
  std::thread reader([&]() {
    CSVChunk chunk;
    chunk.reserve(CHUNK_SIZE);

    std::string line;
    while (std::getline(file, line)) {
      // Parse and get projected fields
      auto split_line = split_csv_line(line, ',');

      std::vector<std::string> projectedFields;
      projectedFields.reserve(projected_indexes.size());
      for (int idx : projected_indexes) {
        if (idx >= 0 && idx < (int)split_line.size()) {
          projectedFields.push_back(split_line[idx]);
        } else {
          projectedFields.push_back("");
        }
      }

      // Add to chunk
      chunk.push_back(std::move(projectedFields));
      if (chunk.size() >= CHUNK_SIZE) {
        lineQueue.push(std::move(chunk));
        chunk.clear();
        chunk.reserve(CHUNK_SIZE);
      }
    }
    // push last partial chunk if not empty
    if (!chunk.empty()) {
      lineQueue.push(std::move(chunk));
    }
    file.close();
    lineQueue.set_finished();
  });

  // --------------------------------------
  // Producer threads: transform chunk of CSV -> chunk of triple strings
  unsigned int NUM_PRODUCERS = std::thread::hardware_concurrency();
  if (NUM_PRODUCERS == 0) {
    NUM_PRODUCERS = 2;  // fallback
  }

  std::vector<std::thread> producers;
  producers.reserve(NUM_PRODUCERS);

  for (unsigned int i = 0; i < NUM_PRODUCERS; i++) {
    producers.emplace_back([&]() {
      size_t local_triple_count = 0;  // Thread-local counter
      CSVChunk csvChunk;
      while (lineQueue.pop(csvChunk)) {
        // Log current queue size (this logs the lineQueue size in the producer
        // thread)
        // std::cout << "[Producer " << std::this_thread::get_id()
        //          << "] lineQueue size: " << lineQueue.size() << "\n";

        // Build a TripleChunk for the current CSV chunk.
        TripleChunk tripleChunk;
        tripleChunk.reserve(csvChunk.size());

        for (auto& rowFields : csvChunk) {
          // Create a map from projected_header.
          std::unordered_map<std::string, std::string> rowMap;
          for (size_t j = 0; j < rowFields.size(); j++) {
            rowMap[projected_header[j]] = rowFields[j];
          }

          // Check for NULL values
          bool skip = false;
          for (const auto& target : values_to_skip) {
            if (std::any_of(rowMap.begin(), rowMap.end(), [&target](const auto& pair) {
                  return pair.second == target;
                })) {
              skip = true;
              break;
            }
          }
          if (skip) {
            continue;
          }

          // Create subject, predicate, object.
          std::string subject, predicate, object, graph;
          try {
            if (s_content[1] == "preformatted") {
              subject = s_content[0];
            } else {
              subject = create_operator(s_content[0], s_content[1], s_content[2], "", "", base_uri, rowMap);
            }
            if (p_content[1] == "preformatted") {
              predicate = p_content[0];
            } else {
              predicate = create_operator(p_content[0], p_content[1], p_content[2], "", "", base_uri, rowMap);
            }
            if (o_content[1] == "preformatted") {
              object = o_content[0];
            } else {
              object = create_operator(o_content[0], o_content[1], o_content[2], o_content[3], o_content[4], base_uri, rowMap);
            }
            if (g_content[1] == "preformatted") {
              graph = g_content[0];
            } else {
              graph = create_operator(g_content[0], g_content[1], g_content[2], "", "", base_uri, rowMap);
            }
          } catch (const std::runtime_error& e) {
            if (!continue_on_error) {
              std::cout << e.what() << std::endl;
              std::exit(1);
            } else {
              continue;
            }
          }

          std::string tripleStr = subject + " " + predicate + " " + object + " " + graph + " .\n";
          tripleChunk.push_back(std::move(tripleStr));
        }

        // Accumulate count.
        local_triple_count += tripleChunk.size();

        // Push the chunk
        tripleQueue.push(std::move(tripleChunk));

        // Optionally, log tripleQueue size as well
        // std::cout << "[Producer " << std::this_thread::get_id()
        //          << "] tripleQueue size: " << tripleQueue.size() << "\n";

        // Update the global atomic counter
        triple_count.fetch_add(local_triple_count, std::memory_order_relaxed);
        local_triple_count = 0;
      }
      // Flush any remaining count
      if (local_triple_count > 0) {
        triple_count.fetch_add(local_triple_count, std::memory_order_relaxed);
      }
    });
  }
  // --------------------------------------
  // Consumer thread: write triple chunks to file
  std::thread consumer([&]() {
    fs::create_directories(output_file_name.parent_path());

    std::ofstream outputFile(output_file_name, std::ios::app);
    if (!outputFile) {
      std::cerr << "Cannot open output file: " << output_file_name << "\n";
      return;
    }
    std::unordered_set<uint64_t> global_hashes;
    global_hashes.reserve(1024 * 1024 * 2);
    // pop entire TripleChunks and write them in one go
    TripleChunk tripleChunk;
    size_t counter = 0;
    std::string buffer = "";
    buffer.reserve(1024 * 30);
    while (tripleQueue.pop(tripleChunk)) {
      // Write everything
      for (auto& t : tripleChunk) {
        // Deduplicate
        std::vector<std::string> val = {t};
        uint64_t rowHash = combinedHash(val);
        if (global_hashes.find(rowHash) != global_hashes.end()) {
          // skip
          continue;
        }
        global_hashes.insert(rowHash);
        buffer += t;
        counter++;

        if (counter == 500) {
          outputFile << buffer;
          buffer = "";
          counter = 0;
        }
      }
    }
    // Flush any remaining content in the buffer
    if (!buffer.empty()) {
      outputFile << buffer;
    }
    outputFile.close();
  });

  // --------------------------------------
  // Wait for everything to finish
  reader.join();
  for (auto& th : producers) {
    th.join();
  }

  tripleQueue.set_finished();
  consumer.join();

  return triple_count.load();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
/// C wrapper
//////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" {
size_t simple_threaded_mapping(const char* information) {
  std::string info(information);
  std::vector<std::string> split_plans = split_by_substring(info, "PxPwPePrP");

  // Remove ""
  split_plans.erase(std::remove_if(split_plans.begin(), split_plans.end(), [](const std::string& s) { return s.empty(); }), split_plans.end());

  if (split_plans.size() != 1) {
    // Execute dependent triple maps
    std::unordered_set<uint64_t> global_hashes;
    global_hashes.reserve(1024 * 1024 * 2);

    std::string output_file;
    for (const auto& plan : split_plans) {
      std::vector<std::string> split_info = split_by_substring(plan, "\n");
      if (split_info.size() != 5) {
        std::cerr << "Plan is too long or short. Got size: " << split_info.size() << "\n";
        for (const auto& val : split_info) {
          std::cout << val << std::endl;
        }
        std::exit(1);
      }
      output_file = split_info[2];
      fs::path output_file_name = split_info[2];
      std::string base_uri = split_info[3];

      std::vector<std::string> split_info_first = split_by_substring(split_info[0], "|||");
      std::vector<std::string> split_info_second = split_by_substring(split_info[1], "|||");

      std::string input_file_name = split_info_first[1];
      std::vector<std::string> projected_attributes = split_by_substring(split_info_first[2], "===");

      // example: "ex:Person/{id}===template===iri"
      std::vector<std::string> s_content = split_by_substring(split_info_second[1], "===");
      std::vector<std::string> p_content = split_by_substring(split_info_second[2], "===");
      std::vector<std::string> o_content = split_by_substring(split_info_second[3], "===");

      global_hashes = execute_dependent(input_file_name, output_file_name, base_uri, projected_attributes, s_content, p_content, o_content, global_hashes);
    }
    // Write to file
    // Serialize the unique triples.
    size_t triple_number = global_hashes.size();

    return triple_number;
  } else {
    // Execute independent triple maps
    std::vector<std::string> split_info = split_by_substring(info, "\n");
    if (split_info.size() != 5) {
      std::cerr << "Plan is too long or short. Got size: " << split_info.size() << "\n";
      std::exit(1);
    }

    fs::path output_file_name = split_info[2];
    std::string base_uri = split_info[3];

    std::vector<std::string> split_info_first = split_by_substring(split_info[0], "|||");
    std::vector<std::string> split_info_second = split_by_substring(split_info[1], "|||");

    std::string input_file_name = split_info_first[1];
    std::vector<std::string> projected_attributes = split_by_substring(split_info_first[2], "===");

    // example: "ex:Person/{id}===template===iri"
    std::vector<std::string> s_content = split_by_substring(split_info_second[1], "===");
    std::vector<std::string> p_content = split_by_substring(split_info_second[2], "===");
    std::vector<std::string> o_content = split_by_substring(split_info_second[3], "===");

    size_t res;
    if (split_info_second.size() == 5) {
      // If split_info is 5 == graph is included
      std::vector<std::string> g_content = split_by_substring(split_info_second[4], "===");
      res = execute_with_graph(input_file_name, output_file_name, base_uri, projected_attributes, s_content, p_content, o_content, g_content);
    } else {
      // Otherwise execute normal
      res = execute(input_file_name, output_file_name, base_uri, projected_attributes, s_content, p_content, o_content);
    }

    return res;
  }
}
}
