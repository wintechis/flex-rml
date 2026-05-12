#include <atomic>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <exception>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>
#include <algorithm>

#include "complex_executor.h"
#include "definitions.h"
#include "simple_executor.h"
#include "utils.h"

class ThreadPool {
 public:
  ThreadPool(size_t numThreads);
  ~ThreadPool();

  void enqueue(std::function<void()> task);
  void shutdown();
  void rethrow_if_failed() const;

 private:
  std::vector<std::thread> workers;
  std::queue<std::function<void()>> tasks;

  std::mutex queueMutex;
  std::condition_variable condition;
  bool stop;
  bool shutdownCalled;
  std::exception_ptr firstException;
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
        try {
          task();
        } catch (...) {
          std::unique_lock<std::mutex> lock(this->queueMutex);
          if (!this->firstException) {
            this->firstException = std::current_exception();
          }
          std::queue<std::function<void()>> empty;
          this->tasks.swap(empty);
          this->stop = true;
          this->condition.notify_all();
        }
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

void ThreadPool::rethrow_if_failed() const {
  if (firstException) {
    std::rethrow_exception(firstException);
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
// Function to split json into key values pairs of filename and data
void split_to_kv_into(std::unordered_map<std::string, std::string>& out,
                      const std::string& s,
                      const std::string& delim = "===|||==="){
    const std::size_t pos = s.find(delim);
    if (pos == std::string::npos) {
        throw std::runtime_error("delimiter not found: " + s);
    }

    std::string key   = s.substr(0, pos);
    std::string value = s.substr(pos + delim.size());

    out[key] = value; // overwrites if key already exists
}

//////////////////////////////////////////////////////////////

std::string execute_physical_plans_string(const std::string& information,
                                          const std::string& mode,
                                          const std::string& continue_error,
                                          const std::string& output_file_path,
                                          bool keep_in_memory,
                                          const std::string& json_data) {
  std::string continue_error_str(continue_error);
  bool continue_on_error = false;
  if (continue_error_str == "true") {
    continue_on_error = true;
  }
  
  std::string threading_enabled(mode);
  std::string info(information);
  std::string ouput_file(output_file_path);

  std::string output_data_str = "";

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
  ///////////////////////
  // Process json data //
  std::unordered_map<std::string, std::string> data_map; // Stores json data
  if (json_data != ""){
    std::vector<std::string> json_data_entries = split_by_substring(json_data, "|||===|||");
    // remove empty entries
    json_data_entries.erase(std::remove_if(json_data_entries.begin(), json_data_entries.end(),[](const std::string& s){ return s.empty(); }), json_data_entries.end());
    for(const auto& data: json_data_entries ){
      split_to_kv_into(data_map, data);
    }
  }


  //////////////////////////////////////////////////////////////////////////////////////////////////////77
  /// EXECUTE PLANS ///
  if (threading_enabled == "false") {
    /// SINGLE THREADED EXECUTION ///
    for (const auto& partition : partitions) {

      bool has_function_call = (partition[0].find("==FUNC==") != std::string::npos);

      // CASE 1: Partition contains only one element and do not keep in memory and no function call
      if (partition.size() == 1 && !keep_in_memory && !has_function_call) {
        std::string plan_str = partition[0];
        int plan_size = split_by_substring(plan_str, "\n").size();
        if (plan_size == 5) {
          nr_generate_triple += standalone_simple_mapping(plan_str, data_map);
        } else if (plan_size == 7) {
          nr_generate_triple += standalone_complex_mapping(plan_str, data_map);
        }
      }
      // CASE 2: Partition contains multiple elements
      else {
        std::unordered_set<std::string> unique_triple;
        for (const auto& plan_str : partition) {
          int plan_size = split_by_substring(plan_str, "\n").size();
          if (plan_size == 5) {
            unique_triple = dependent_simple_mapping(plan_str, unique_triple, data_map);
          } else if (plan_size == 7) {
            unique_triple = dependent_complex_mapping(plan_str, unique_triple, data_map);
          }
        }

        nr_generate_triple += unique_triple.size();

        // serialize //
        std::string buffer;
        buffer.reserve(1024 * 1024);

        for (const auto& triple : unique_triple) {
          buffer += triple;  // data to buffer
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
    std::mutex output_mutex;

    // Enqueue each partition as a task.
    for (const auto& partition : partitions) {

      bool has_function_call = (partition[0].find("==FUNC==") != std::string::npos);

      pool.enqueue([partition, &nr_generate_triple, &output_mutex, &ouput_file, keep_in_memory, &output_data_str, &data_map, &has_function_call]() {
        // CASE 1: Partition contains only one element.
        if (partition.size() == 1 && !keep_in_memory && !has_function_call) {
          std::string plan_str = partition[0];
          int plan_size = split_by_substring(plan_str, "\n").size();
          if (plan_size == 5) {
            nr_generate_triple.fetch_add(standalone_simple_mapping(plan_str, data_map), std::memory_order_relaxed);
          } else if (plan_size == 7) {
            nr_generate_triple.fetch_add(standalone_complex_mapping(plan_str, data_map), std::memory_order_relaxed);
          }
        }
        // CASE 2: Partition contains multiple elements.
        else {
          std::unordered_set<std::string> unique_triple;
          for (const auto& plan_str : partition) {
            int plan_size = split_by_substring(plan_str, "\n").size();
            if (plan_size == 5) {
              unique_triple = dependent_simple_mapping(plan_str, unique_triple, data_map);
            } else if (plan_size == 7) {
              unique_triple = dependent_complex_mapping(plan_str, unique_triple, data_map);
            }
          }
          nr_generate_triple.fetch_add(unique_triple.size(), std::memory_order_relaxed);

          // Serialize the unique triples.
          std::string buffer;
          buffer.reserve(1024 * 1024);
          for (const auto& triple : unique_triple) {
            buffer += triple;
          }
      
          // Protect file writing using a mutex.
          std::lock_guard<std::mutex> lock(output_mutex);
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
      });
    }

    // Shutdown the pool to ensure all tasks finish.
    pool.shutdown();
    pool.rethrow_if_failed();
  } 
  return std::to_string(nr_generate_triple.load()) + "|||" + output_data_str;
}
