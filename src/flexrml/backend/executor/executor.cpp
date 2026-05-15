#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
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
#include "physical_plan.h"
#include "simple_executor.h"
#include "utils.h"

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kOutputBufferReserve = 64 * 1024;
constexpr std::size_t kMaxQueuedOutputBytes = 64 * 1024 * 1024;
constexpr std::size_t kMinGraphFusedPlansForParallelSplit = 8;
constexpr std::size_t kMinPlainFusedPlansForParallelSplit = 30;

void write_triples_chunked(std::ostream& output, const std::unordered_set<std::string>& triples) {
  std::string buffer;
  buffer.reserve(kOutputBufferReserve);

  for (const auto& triple : triples) {
    if (!buffer.empty() && buffer.size() + triple.size() > kOutputBufferReserve) {
      output << buffer;
      buffer.clear();
    }
    buffer += triple;
  }

  if (!buffer.empty()) {
    output << buffer;
  }
}

void write_triples_chunked(OutputChunkWriter& output, const std::unordered_set<std::string>& triples) {
  std::string buffer;
  buffer.reserve(kOutputBufferReserve);

  for (const auto& triple : triples) {
    if (!buffer.empty() && buffer.size() + triple.size() > kOutputBufferReserve) {
      output.write(std::move(buffer));
      buffer.clear();
      buffer.reserve(kOutputBufferReserve);
    }
    buffer += triple;
  }

  if (!buffer.empty()) {
    output.write(std::move(buffer));
  }
}

void append_triples(std::string& output, const std::unordered_set<std::string>& triples) {
  for (const auto& triple : triples) {
    output += triple;
  }
}

bool is_single_constant_simple_plan(const SimplePlan& plan) {
  if (!plan.generate_graph) {
    return (plan.s_content[1] == "constant" && plan.p_content[1] == "constant" && plan.o_content[1] == "constant") ||
           (plan.s_content[1] == "preformatted" && plan.p_content[1] == "preformatted" && plan.o_content[1] == "preformatted");
  }
  return (plan.s_content[1] == "constant" && plan.p_content[1] == "constant" && plan.o_content[1] == "constant" && plan.g_content[1] == "constant") ||
         (plan.s_content[1] == "preformatted" && plan.p_content[1] == "preformatted" && plan.o_content[1] == "preformatted" && plan.g_content[1] == "preformatted");
}

bool can_fuse_simple_partition(const PlanPartition& partition) {
  if (partition.plans.size() < 2 || partition.has_function_call) {
    return false;
  }

  const SimplePlan* first = nullptr;
  for (const auto& plan : partition.plans) {
    if (plan.kind != PhysicalPlanKind::Simple || is_single_constant_simple_plan(plan.simple)) {
      return false;
    }
    if (first == nullptr) {
      first = &plan.simple;
      continue;
    }
    if (plan.simple.input_file_name != first->input_file_name ||
        plan.simple.output_file_name != first->output_file_name) {
      return false;
    }
  }
  return true;
}

bool is_standalone_complex_partition(const PlanPartition& partition) {
  return partition.plans.size() == 1 &&
         !partition.has_function_call &&
         partition.plans[0].kind == PhysicalPlanKind::Complex;
}

std::vector<SimplePlan> collect_simple_plans(const PlanPartition& partition) {
  std::vector<SimplePlan> plans;
  plans.reserve(partition.plans.size());
  for (const auto& plan : partition.plans) {
    plans.push_back(plan.simple);
  }
  return plans;
}

bool should_parallelize_fused_partition(const PlanPartition& partition, bool keep_in_memory, bool can_use_shared_writer) {
  if (keep_in_memory ||
      !can_use_shared_writer ||
      !can_fuse_simple_partition(partition)) {
    return false;
  }

  const bool all_graph_plans = std::all_of(partition.plans.begin(), partition.plans.end(), [](const PhysicalPlan& plan) {
    return plan.kind == PhysicalPlanKind::Simple && plan.simple.generate_graph;
  });
  const std::size_t threshold = all_graph_plans
                                    ? kMinGraphFusedPlansForParallelSplit
                                    : kMinPlainFusedPlansForParallelSplit;
  return partition.plans.size() >= threshold;
}

}  // namespace

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

class SharedOutputWriter : public OutputChunkWriter {
 public:
  explicit SharedOutputWriter(const std::string& output_path)
      : output_path(output_path), output(output_path, std::ios::app | std::ios::binary), closed(false) {
    if (!output) {
      throw std::runtime_error("Unable to open output file for writing: " + output_path);
    }

    worker = std::thread([this] { run(); });
  }

  ~SharedOutputWriter() override {
    try {
      close();
    } catch (...) {
    }
  }

  void write(std::string chunk) override {
    if (chunk.empty()) {
      return;
    }

    const std::size_t chunk_size = chunk.size();
    {
      std::unique_lock<std::mutex> lock(queue_mutex);
      condition.wait(lock, [this, chunk_size] {
        return closed || queued_bytes + chunk_size <= kMaxQueuedOutputBytes;
      });
      if (closed) {
        rethrow_if_failed();
        throw std::runtime_error("Cannot write to closed output writer.");
      }
      queued_bytes += chunk_size;
      chunks.push(std::move(chunk));
    }
    condition.notify_one();
  }

  void close() {
    {
      std::unique_lock<std::mutex> lock(queue_mutex);
      closed = true;
    }
    condition.notify_all();
    if (worker.joinable()) {
      worker.join();
    }
    rethrow_if_failed();
  }

 private:
  void run() {
    try {
      for (;;) {
        std::string chunk;
        {
          std::unique_lock<std::mutex> lock(queue_mutex);
          condition.wait(lock, [this] { return closed || !chunks.empty(); });
          if (chunks.empty()) {
            if (closed) {
              break;
            }
            continue;
          }
          chunk = std::move(chunks.front());
          chunks.pop();
          queued_bytes -= chunk.size();
          condition.notify_all();
        }
        output.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        if (!output) {
          throw write_failure(chunk.size());
        }
      }
      output.flush();
      if (!output) {
        throw write_failure(0);
      }
    } catch (...) {
      std::unique_lock<std::mutex> lock(queue_mutex);
      if (!first_exception) {
        first_exception = std::current_exception();
      }
      closed = true;
      std::queue<std::string> empty;
      chunks.swap(empty);
      queued_bytes = 0;
      condition.notify_all();
    }
  }

  std::runtime_error write_failure(std::size_t attempted_bytes) const {
    std::string message = "Failed while writing output to " + output_path;
    if (attempted_bytes > 0) {
      message += " (" + std::to_string(attempted_bytes) + " bytes)";
    }

    const int error_number = errno;
    if (error_number != 0) {
      message += ": " + std::string(std::strerror(error_number));
    }

    std::error_code error;
    const auto parent = fs::path(output_path).parent_path();
    const auto space = fs::space(parent.empty() ? fs::path(".") : parent, error);
    if (!error) {
      message += "; available space: " + std::to_string(space.available) + " bytes";
    }

    return std::runtime_error(message);
  }

  void rethrow_if_failed() const {
    if (first_exception) {
      std::rethrow_exception(first_exception);
    }
  }

  std::string output_path;
  std::ofstream output;
  std::thread worker;
  std::queue<std::string> chunks;
  std::mutex queue_mutex;
  std::condition_variable condition;
  std::size_t queued_bytes = 0;
  bool closed;
  std::exception_ptr first_exception;
};

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

std::unordered_map<std::string, std::string> parse_json_data_map(const std::string& json_data) {
  std::unordered_map<std::string, std::string> data_map;
  if (json_data != "") {
    std::vector<std::string> json_data_entries = split_by_substring(json_data, "|||===|||");
    json_data_entries.erase(std::remove_if(json_data_entries.begin(), json_data_entries.end(), [](const std::string& s) { return s.empty(); }), json_data_entries.end());
    for (const auto& data : json_data_entries) {
      split_to_kv_into(data_map, data);
    }
  }
  return data_map;
}

//////////////////////////////////////////////////////////////

std::string execute_physical_plan_partitions(const std::vector<PlanPartition>& partitions,
                                             const std::string& mode,
                                             const std::string& output_file_path,
                                             bool keep_in_memory,
                                             const std::string& json_data) {
  std::string threading_enabled(mode);
  std::string ouput_file(output_file_path);

  std::string output_data_str = "";

  if (!keep_in_memory){
    clear_output_file(ouput_file);
  }

  std::atomic<int> nr_generate_triple(0);
  std::unordered_map<std::string, std::string> data_map = parse_json_data_map(json_data);

  //////////////////////////////////////////////////////////////////////////////////////////////////////77
  /// EXECUTE PLANS ///
  if (threading_enabled == "false") {
    /// SINGLE THREADED EXECUTION ///
    for (const auto& partition : partitions) {
      // CASE 1: Partition contains only one element and do not keep in memory and no function call
      if (partition.plans.size() == 1 && !keep_in_memory && !partition.has_function_call) {
        const PhysicalPlan& plan = partition.plans[0];
        if (plan.kind == PhysicalPlanKind::Simple) {
          nr_generate_triple += execute_standalone_simple_plan(plan.simple, data_map);
        } else {
          nr_generate_triple += execute_standalone_complex_plan(plan.complex, data_map);
        }
      }
      // CASE 2: Partition contains multiple elements
      else {
        if (!keep_in_memory && can_fuse_simple_partition(partition)) {
          nr_generate_triple += execute_fused_simple_plans(collect_simple_plans(partition), data_map, nullptr);
          continue;
        }

        std::unordered_set<std::string> unique_triple;
        for (const auto& plan : partition.plans) {
          if (plan.kind == PhysicalPlanKind::Simple) {
            unique_triple = execute_dependent_simple_plan(plan.simple, unique_triple, data_map);
          } else {
            unique_triple = execute_dependent_complex_plan(plan.complex, unique_triple, data_map);
          }
        }

        nr_generate_triple += unique_triple.size();

        if (keep_in_memory){
          append_triples(output_data_str, unique_triple);
        } else {
          std::ofstream outputFile(ouput_file, std::ios::app);
          if (!outputFile) {
            std::cout << "Error: Unable to open file for writing." << std::endl;
            std::exit(1);
          }
          write_triples_chunked(outputFile, unique_triple);
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
    bool can_use_shared_writer = !keep_in_memory;

    std::unique_ptr<SharedOutputWriter> shared_writer;
    if (can_use_shared_writer) {
      shared_writer = std::make_unique<SharedOutputWriter>(ouput_file);
    }
    OutputChunkWriter* shared_writer_ptr = shared_writer.get();
    std::vector<PlanPartition> deferred_standalone_complex_partitions;

    // Enqueue each partition as a task.
    for (const auto& partition : partitions) {
      if (shared_writer_ptr != nullptr && is_standalone_complex_partition(partition)) {
        deferred_standalone_complex_partitions.push_back(partition);
        continue;
      }

      if (should_parallelize_fused_partition(partition, keep_in_memory, can_use_shared_writer)) {
        const auto simple_plans = collect_simple_plans(partition);
        const std::size_t chunk_count = std::min<std::size_t>(numThreads, simple_plans.size());
        const std::size_t chunk_size = (simple_plans.size() + chunk_count - 1) / chunk_count;
        auto shared_deduper = create_concurrent_triple_deduper();

        for (std::size_t chunk_begin = 0; chunk_begin < simple_plans.size(); chunk_begin += chunk_size) {
          const std::size_t chunk_end = std::min(simple_plans.size(), chunk_begin + chunk_size);
          std::vector<SimplePlan> chunk(simple_plans.begin() + static_cast<std::ptrdiff_t>(chunk_begin),
                                        simple_plans.begin() + static_cast<std::ptrdiff_t>(chunk_end));
          pool.enqueue([chunk = std::move(chunk), shared_deduper, &nr_generate_triple, &data_map, shared_writer_ptr]() {
            nr_generate_triple.fetch_add(
                execute_fused_simple_plans(chunk, data_map, shared_writer_ptr, shared_deduper.get()),
                std::memory_order_relaxed);
          });
        }
        continue;
      }

      pool.enqueue([partition, &nr_generate_triple, &output_mutex, &ouput_file, keep_in_memory, &output_data_str, &data_map, shared_writer_ptr]() {
        // CASE 1: Partition contains only one element.
        if (partition.plans.size() == 1 && !keep_in_memory && !partition.has_function_call) {
          const PhysicalPlan& plan = partition.plans[0];
          if (plan.kind == PhysicalPlanKind::Simple) {
            if (shared_writer_ptr != nullptr) {
              nr_generate_triple.fetch_add(execute_standalone_simple_plan(plan.simple, data_map, shared_writer_ptr), std::memory_order_relaxed);
            } else {
              std::lock_guard<std::mutex> lock(output_mutex);
              nr_generate_triple.fetch_add(execute_standalone_simple_plan(plan.simple, data_map), std::memory_order_relaxed);
            }
          } else {
            std::lock_guard<std::mutex> lock(output_mutex);
            nr_generate_triple.fetch_add(execute_standalone_complex_plan(plan.complex, data_map), std::memory_order_relaxed);
          }
        }
        // CASE 2: Partition contains multiple elements.
        else {
          if (!keep_in_memory && can_fuse_simple_partition(partition)) {
            const auto simple_plans = collect_simple_plans(partition);
            if (shared_writer_ptr != nullptr) {
              nr_generate_triple.fetch_add(execute_fused_simple_plans(simple_plans, data_map, shared_writer_ptr), std::memory_order_relaxed);
            } else {
              std::lock_guard<std::mutex> lock(output_mutex);
              nr_generate_triple.fetch_add(execute_fused_simple_plans(simple_plans, data_map, nullptr), std::memory_order_relaxed);
            }
            return;
          }

          std::unordered_set<std::string> unique_triple;
          for (const auto& plan : partition.plans) {
            if (plan.kind == PhysicalPlanKind::Simple) {
              unique_triple = execute_dependent_simple_plan(plan.simple, unique_triple, data_map);
            } else {
              unique_triple = execute_dependent_complex_plan(plan.complex, unique_triple, data_map);
            }
          }
          nr_generate_triple.fetch_add(unique_triple.size(), std::memory_order_relaxed);

          if (keep_in_memory){
            std::lock_guard<std::mutex> lock(output_mutex);
            append_triples(output_data_str, unique_triple);
          } else if (shared_writer_ptr != nullptr) {
            write_triples_chunked(*shared_writer_ptr, unique_triple);
          } else {
            std::lock_guard<std::mutex> lock(output_mutex);
            std::ofstream outputFile(ouput_file, std::ios::app);
            if (!outputFile) {
              std::cout << "Error: Unable to open file for writing." << std::endl;
              std::exit(1);
            }
            write_triples_chunked(outputFile, unique_triple);
            outputFile.close();
          }
          
        }
      });
    }

    // Shutdown the pool to ensure all tasks finish.
    pool.shutdown();
    if (shared_writer) {
      shared_writer->close();
    }
    pool.rethrow_if_failed();

    for (const auto& partition : deferred_standalone_complex_partitions) {
      nr_generate_triple.fetch_add(
          execute_standalone_complex_plan(partition.plans[0].complex, data_map),
          std::memory_order_relaxed);
    }
  } 
  return std::to_string(nr_generate_triple.load()) + "|||" + output_data_str;
}
