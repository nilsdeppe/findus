// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <atomic>
#include <cstddef>
#include <hwloc.h>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

#include "ConcurrentQueue.hpp"
#include "Rts/QuiescenceDetection.hpp"
#include "Spinlock.hpp"

namespace rts {
/*!
 *
 *
 * Basic ideas from:
 *   https://stackoverflow.com/questions/15752659/thread-pooling-in-c11
 *
 * Other useful links:
 * https://www.1024cores.net/home/lock-free-algorithms/queues/queue-catalog
 * https://moodycamel.com/blog/2014/a-fast-general-purpose-lock-free-queue-for-c++
 * https://iditkeidar.com/wp-content/uploads/files/ftp/spaa049-gidron.pdf
 */
template <class MessageType, class ProcessLocalDataType>
class ThreadPool {
 public:
  ThreadPool() = default;
  ThreadPool(uint32_t number_of_threads, uint32_t thread_pin_offset,
             ProcessLocalDataType process_local_data_for_execution);

  /// \brief Pin the thread with ID `thread_id` to a core, then call
  /// `thread_loop(thread_id);`
  void pin_and_thread_loop(uint32_t thread_id,
                           std::optional<uint32_t> thread_to_print_from);

  /// \brief Busy loop on each thread that grabs tasks from the task queue.
  void thread_loop(uint32_t thread_id,
                   std::optional<uint32_t> thread_to_print_from);

  /// \brief The task `message` is added to the queue from thread with thread ID
  /// `thread_id`.
  ///
  /// The `thread_id` is the ID of the _current_ thread.
  void add_task(uint32_t thread_id, MessageType message);

  /// \brief The task `message` is added to the queue without knowing what
  /// thread it came from.
  ///
  /// This can be slightly slower than if you know the inserting thread.
  void add_task(MessageType message);

  /*!
   * \brief Adds multiple tasks.
   *
   * `number_of_messages` is the number of `messages` there are.
   *
   * `MessageIt` must be an (legacy) input iterator. I.e. is has
   * `operator++()`, `operator++(int)` and `operator*()` defined.
   */
  template <class MessageIt>
  void add_tasks(MessageIt messages, const size_t number_of_messages);

  /// \brief Launch all the threads and pin them to a core..
  void launch_threads(std::optional<uint32_t> thread_to_print_from);

  /// \brief Stop all threads.
  void stop() {
    stop_threads_.store(true, std::memory_order_release);
    for (std::thread& active_thread : threads_) {
        active_thread.join();
    }
    threads_.clear();
  }

  /// \brief Returns `true` if all threads are idle and quiescence can be
  /// guaranteed.
  ///
  /// You need to call this function at least twice to verify that quiescence
  /// has been reached. This is because it is not possible to determine
  /// quiesecence instantaneously.
  bool is_quiescent() {
    return local_qd_.is_quiescent(static_cast<std::int64_t>(threads_.size()));
  }

  /// \brief Log to `std::cout`
  void print_to(std::string to_print) {
    logging_queue_.enqueue(std::move(to_print));
  }

 private:
#ifdef __cpp_lib_hardware_interference_size
  static constexpr std::size_t hardware_destructive_interference_size =
      std::hardware_destructive_interference_size;
#else
  static constexpr std::size_t hardware_destructive_interference_size = 64;
#endif

  // Task design:
  // - We have a list of tasks for each thread. Another design is to have one
  //   task list for _all_ threads. A single task list has the advantage of
  //   being able to automatically load balance within a node. This downside is
  //   then we need some way of determining if a task can safely be run to
  //   avoid race conditions.
  // - The big question is what should the queue of tasks hold? std::function
  //   is one (expensive) option.
  ProcessLocalDataType process_local_data_for_execution_;
  uint32_t thread_pin_offset_ = 0;
  std::vector<std::thread> threads_{};
  alignas(hardware_destructive_interference_size)
      std::atomic<bool> stop_threads_{false};
  alignas(hardware_destructive_interference_size)
      moodycamel::ConcurrentQueue<MessageType> task_queue_{};
  std::vector<moodycamel::ProducerToken> producer_tokens_{};
  std::vector<moodycamel::ConsumerToken> consumer_tokens_{};

  qd::Local local_qd_{};

  moodycamel::ConcurrentQueue<std::string> logging_queue_{};

  hwloc_topology_t topology_{};
};

template <class MessageType, class ProcessLocalDataType>
inline ThreadPool<MessageType, ProcessLocalDataType>::ThreadPool(
    const uint32_t number_of_threads, const uint32_t thread_pin_offset,
    ProcessLocalDataType process_local_data_for_execution)
    : process_local_data_for_execution_(
          std::move(process_local_data_for_execution)),
      thread_pin_offset_(thread_pin_offset),
      threads_(number_of_threads),
      stop_threads_{false},
      // Static size for 1024 tasks per thread.
      task_queue_(1024, number_of_threads, 0),
      local_qd_{},
      logging_queue_{} {
  // Pin the threads to CPU cores. This is often called "affinity"
  //
  // Modified from:
  // https://github.com/eliben/code-for-blog/blob/master/2016/threads-affinity/hwloc-example.cpp
  if (hwloc_topology_init(&topology_) < 0) {
    throw std::runtime_error("error calling hwloc_topology_init");
  }
  if (hwloc_topology_load(topology_) < 0) {
    throw std::runtime_error("error calling hwloc_topology_load");
  }
  // PU=processing unit, which are hardware threads.
  [[maybe_unused]] const int number_of_processing_units =
      hwloc_get_nbobjs_by_type(topology_, hwloc_obj_type_t::HWLOC_OBJ_PU);
  const int number_of_cores =
      hwloc_get_nbobjs_by_type(topology_, hwloc_obj_type_t::HWLOC_OBJ_CORE);
  if (number_of_cores < thread_pin_offset_ + number_of_threads) {
    throw std::runtime_error(
        "There are fewer cores than the offset and number of threads can "
        "accomodate");
  }

  producer_tokens_.reserve(number_of_threads);
  consumer_tokens_.reserve(number_of_threads);
  for (uint32_t i = 0; i < number_of_threads; i++) {
    producer_tokens_.emplace_back(task_queue_);
    consumer_tokens_.emplace_back(task_queue_);
  }
}

template <class MessageType, class ProcessLocalDataType>
inline void ThreadPool<MessageType, ProcessLocalDataType>::launch_threads(
    const std::optional<uint32_t> thread_to_print_from) {
  for (size_t i = 0; i < threads_.size(); i++) {
    threads_[i] = std::thread(&ThreadPool::pin_and_thread_loop, this, i,
                              thread_to_print_from);
  }
}

template <class MessageType, class ProcessLocalDataType>
inline void ThreadPool<MessageType, ProcessLocalDataType>::pin_and_thread_loop(
    const uint32_t thread_id,
    const std::optional<uint32_t> thread_to_print_from) {
  hwloc_obj_t core_to_pin =
      hwloc_get_obj_by_type(topology_, hwloc_obj_type_t::HWLOC_OBJ_CORE,
                            thread_pin_offset_ + thread_id);

  if (hwloc_set_cpubind(topology_, core_to_pin->cpuset, HWLOC_CPUBIND_THREAD) <
      0) {
    throw std::runtime_error("Error calling hwloc_set_cpubind\n");
  }
  return thread_loop(thread_id, thread_to_print_from);
}

template <class MessageType, class ProcessLocalDataType>
inline void ThreadPool<MessageType, ProcessLocalDataType>::thread_loop(
    const uint32_t thread_id,
    const std::optional<uint32_t> thread_to_print_from) {
  // We use the miss_count to keep track of how many messages we failed to
  // evaluate for various different reasons.
  int32_t miss_count = 0;
  const int32_t miss_count_for_idle = 20;
  // const int32_t allowed_misses = 10;
  // Track how often we should try to print messages to screen.
  int32_t call_count_for_print = 0;
  const int32_t call_counts_to_print_at = 20;

  while (true) {
    // Abort if we are told to stop the threads.
    //
    // Use relaxed order since we don't care if we execute one more task or not.
    if (stop_threads_.load(std::memory_order_relaxed)) {
      return;
    }

    // (If we have invoked enough messages that we should print text _or_ we
    // are in idle mode) _AND_ we are the thread responsible for printing,
    // then print any queued messages to screen.
    //
    // Note: we have this check _before_ our dequeue and idle check below so
    // that even if thread `thread_to_print_from` is starved for work for a
    // while it still tries to print text to screen.
    if ((call_count_for_print >= call_counts_to_print_at or
         miss_count == -1) and
        thread_to_print_from.has_value() and
        thread_id == thread_to_print_from.value()) {
      size_t number_to_print = 0;
      do {
        std::array<std::string, 20> messages_to_print{};
        number_to_print = logging_queue_.try_dequeue_bulk(
            messages_to_print.begin(), messages_to_print.size());
        for (size_t i = 0; i < number_to_print; ++i) {
          std::cout << messages_to_print[i];
          messages_to_print[i] = std::string{};
        }
      } while (number_to_print != 0);
      call_count_for_print = 0;
    }

    MessageType message{};

    // Try to dequeue a message.
    //
    // If we fail, then we increment the miss_count if we are not in "idle
    // mode" (indicated by a miss_count of -1).
    //
    // If our miss_count has reached the idle count, then we increment the
    // number of idle threads and set the miss_count to -1 to signal that we
    // are currently in "idle mode".
    //
    // Finally, since we failed to dequeue a message, we failed to dequeue a
    // message, we skip trying to invoke the message.
    if (not task_queue_.try_dequeue(message)) {
      if (miss_count != -1) {
        ++miss_count;
      }
      if (miss_count == miss_count_for_idle) {
        local_qd_.increment_idle_thread_count();
        miss_count = -1;
      }
      continue;
    }

    // If we were idle, then decrement the idle thread count and set the
    // miss_count to zero.
    if (miss_count == -1) {
      local_qd_.decrement_idle_thread_count();
      miss_count = 0;
    }

    // `execute` returns `true` on success and `false` on failure. On
    // failure we need to requeue the message.
    if (MessageType::execute(*this, thread_id, message,
                             process_local_data_for_execution_)) {
      ++call_count_for_print;
      local_qd_.increment_processed();
    } else {
      task_queue_.try_enqueue(std::move(message));
    }
  }
}

template <class MessageType, class ProcessLocalDataType>
inline void ThreadPool<MessageType, ProcessLocalDataType>::add_task(
    const uint32_t thread_id, MessageType message) {
  local_qd_.increment_sent();
  if (not task_queue_.enqueue(producer_tokens_[thread_id],
                              std::move(message))) {
    throw std::runtime_error("Failed to enqueue a message onto the thread");
  }
}

template <class MessageType, class ProcessLocalDataType>
inline void ThreadPool<MessageType, ProcessLocalDataType>::add_task(
    MessageType message) {
  local_qd_.increment_sent();
  if (not task_queue_.enqueue(std::move(message))) {
    throw std::runtime_error("Failed to enqueue a message onto the thread");
  }
}

template <class MessageType, class ProcessLocalDataType>
template <class MessageIt>
inline void ThreadPool<MessageType, ProcessLocalDataType>::add_tasks(
    MessageIt messages, const size_t number_of_messages) {
  for (size_t i = 0; i < number_of_messages; ++i) {
    local_qd_.increment_sent();
  }
  if (not task_queue_.enqueue_bulk(std::move(messages), number_of_messages)) {
    throw std::runtime_error("Failed to enqueue a message onto the thread");
  }
}
}  // namespace rts
