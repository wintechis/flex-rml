#include <atomic>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "complex_executor.h"
#include "definitions.h"
#include "simple_executor.h"
#include "utils.h"

static std::string final_result;

class ThreadPool {
 public:
  ThreadPool(size_t numThreads);
  ~ThreadPool();

  void enqueue(std::function<void()> task);
  void shutdown();

 private:
  std::vector<std::thread> workers;
  std::queue<std::function<void()>> tasks;

  std::mutex queueMutex;
  std::condition_variable condition;
  bool stop;
  bool shutdownCalled;
};

ThreadPool::ThreadPool(size_t numThreads) : stop(false), shutdownCalled(false) {
  for (size_t i = 0; i < numThreads; ++i) {
    workers.emplace_back([this] {
      for (;;) {
        std::function<void()> task;
        {
          std::unique_lock<std::mutex> lock(this->queueMutex);
          this->condition.wait(
              lock, [this] { return this->stop || !this->tasks.empty(); });
          if (this->stop && this->tasks.empty()) return;
          task = std::move(this->tasks.front());
          this->tasks.pop();
        }
        task();
      }
    });
  }
}

void ThreadPool::enqueue(std::function<void()> task) {
  {
    std::unique_lock<std::mutex> lock(queueMutex);
    tasks.push(std::move(task));
  }
  condition.notify_one();
}

void ThreadPool::shutdown() {
  {
    std::unique_lock<std::mutex> lock(queueMutex);
    if (shutdownCalled) return;
    stop = true;
    shutdownCalled = true;
  }
  condition.notify_all();
  for (std::thread& worker : workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

ThreadPool::~ThreadPool() { shutdown(); }

///////////////////////////////////////////////////////////////
void clear_output_file(const std::string& output_file_path) {
  std::ofstream file(output_file_path, std::ios::out | std::ios::trunc);
  if (file.is_open()) {
    file.close();
  } else {
    std::cerr << "Error opening output file! Got output path: " << output_file_path << std::endl;
    std::exit(1);
  }
}

//////////////////////////////////////////////////////////////

extern "C" {
const char *execute_physical_plans(const char* information, const char* mode,
                           const char* continue_error,
                           const char* output_file_path, const char* keep_data_in_memory) {
  // Get config variables //
  std::string continue_error_str(continue_error);
  bool continue_on_error = false;
  if (continue_error_str == "true") {
    continue_on_error = true;
  }
  
  std::string threading_enabled(mode);
  std::string info(information);
  std::string ouput_file(output_file_path);
  

  std::string keep_in_memory_str(keep_data_in_memory);
  bool keep_in_memory = false;
  if (keep_in_memory_str == "true") {
    keep_in_memory = true;
  }

 // If keep in memory store in string
  std::string output_data_str = "";

  // Clear output file
  if (!keep_in_memory){
    clear_output_file(ouput_file);
  }

  // Store number of generated triple
  std::atomic<int> nr_generate_triple(0);

  ///////////////////
  // Process plans //
  std::vector<std::string> plan_partitions = split_by_substring(info, "TTTtttTTTtttTTT");

  std::vector<std::vector<std::string>> partitions;
  for (const auto& partition : plan_partitions) {
    if (partition.empty()) {
      continue;
    }

    std::vector<std::string> separated_plans_str = split_by_substring(partition, "PxPwPePrP");

    // Get all non-empty plans.
    std::vector<std::string> valid_separated_plans_str;
    for (const auto& plan_str : separated_plans_str) {
      if (!plan_str.empty()) {
        valid_separated_plans_str.push_back(plan_str);
      }
    }

    partitions.push_back(valid_separated_plans_str);
  }
  //////////////////////////////////////////////////////////////////////////////////////////////////////77
  /// EXECUTE PLANS ///
  if (threading_enabled == "false") {
    /// SINGLE THREADED EXECUTION ///
    for (const auto& partition : partitions) {
      // CASE 1: Partition contains only one element and do not keep in memory
      if (partition.size() == 1 && !keep_in_memory) {
        std::string plan_str = partition[0];
        int plan_size = split_by_substring(plan_str, "\n").size();
        if (plan_size == 5) {
          nr_generate_triple += standalone_simple_mapping(plan_str);
        } else if (plan_size == 7) {
          nr_generate_triple += standalone_complex_mapping(plan_str);
        }
      }
      // CASE 2: Partition contains multiple elements
      else {
        std::unordered_set<std::string> unique_triple;

        for (const auto& plan_str : partition) {
          int plan_size = split_by_substring(plan_str, "\n").size();
          if (plan_size == 5) {
            unique_triple = dependent_simple_mapping(plan_str, unique_triple);
          } else if (plan_size == 7) {
            unique_triple = dependent_complex_mapping(plan_str, unique_triple);
          }
        }

        nr_generate_triple += unique_triple.size();

        // serialize //
        std::string buffer;
        buffer.reserve(1024 * 1024);

        for (const auto& triple : unique_triple) {
          buffer += triple + "\n";  // data to buffer
        }

        if (keep_in_memory){
          output_data_str += buffer;
        } else {
          std::ofstream outputFile(ouput_file, std::ios::app);
          if (!outputFile) {
            std::cout << "Error: Unable to open file for writing." << std::endl;
            std::exit(1);
          }
          outputFile << buffer;
          outputFile.close();
        }
      }
    }
  } else {
    // THREADED EXECUTION
    // Determine number of threads to use.
    size_t numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) {
      numThreads = 2;
    }
    ThreadPool pool(numThreads);
    std::mutex file_mutex;

    // Enqueue each partition as a task.
    for (const auto& partition : partitions) {
      // protects output_data_str and file writes
      std::mutex out_mutex;

      pool.enqueue([partition, &nr_generate_triple, &file_mutex, &ouput_file, keep_in_memory, &output_data_str, &out_mutex]() {
        // CASE 1: Partition contains only one element.
        if (partition.size() == 1 && !keep_in_memory) {
          std::string plan_str = partition[0];
          int plan_size = split_by_substring(plan_str, "\n").size();
          if (plan_size == 5) {
            nr_generate_triple.fetch_add(standalone_simple_mapping(plan_str), std::memory_order_relaxed);
          } else if (plan_size == 7) {
            nr_generate_triple.fetch_add(standalone_complex_mapping(plan_str), std::memory_order_relaxed);
          }
        }
        // CASE 2: Partition contains multiple elements.
        else {
          std::unordered_set<std::string> unique_triple;
          for (const auto& plan_str : partition) {
            int plan_size = split_by_substring(plan_str, "\n").size();
            if (plan_size == 5) {
              unique_triple = dependent_simple_mapping(plan_str, unique_triple);
            } else if (plan_size == 7) {
              unique_triple = dependent_complex_mapping(plan_str, unique_triple);
            }
          }
          nr_generate_triple.fetch_add(unique_triple.size(), std::memory_order_relaxed);

          // Serialize the unique triples.
          std::string buffer;
          buffer.reserve(1024 * 1024);
          for (const auto& triple : unique_triple) {
            buffer += triple + "\n";
          }
          // Protect file writing using a mutex.
          {
            

            if (keep_in_memory){
              std::lock_guard<std::mutex> lock(out_mutex);
              output_data_str += buffer;
            } else {
              std::lock_guard<std::mutex> lock(file_mutex);
              std::ofstream outputFile(ouput_file, std::ios::app);
              if (!outputFile) {
                std::cout << "Error: Unable to open file for writing."
                          << std::endl;
                std::exit(1);
              }
              outputFile << buffer;
              outputFile.close();
            }
          }
        }
      });
    }

    // Shutdown the pool to ensure all tasks finish.
    pool.shutdown();
  } 
  final_result = std::to_string(nr_generate_triple.load()) + "|||" + output_data_str;
  return final_result.c_str();
}
}
