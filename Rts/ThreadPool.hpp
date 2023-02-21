// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <atomic>
#include <cstddef>
#include <iostream>
#include <thread>
#include <vector>

#include "Spinlock.hpp"

namespace rts {
/*!
 *
 *
 * Basic ideas from:
 *   https://stackoverflow.com/questions/15752659/thread-pooling-in-c11
 */
class ThreadPool {
 public:
  ThreadPool() = default;
  ThreadPool(uint32_t number_of_threads);

  void thread_loop(const uint32_t thread_id);

  void stop() {
    stop_threads_.store(true, std::memory_order_release);
    for (std::thread& active_thread : threads_) {
        active_thread.join();
    }
    threads_.clear();
  }

 private:
  // Task design:
  // - We have a list of tasks for each thread. Another design is to have one
  //   task list for _all_ threads. A single task list has the advantage of
  //   being able to automatically load balance within a node. This downside is
  //   then we need some way of determining if a task can safely be run to
  //   avoid race conditions.
  // - Use a std::queue for each thread, block push/ pop with a spinlock
  // - The big question is what should the queue of tasks hold? std::function
  //   is one (expensive) option.
  // std::vector<std::queue<>> per_thread_tasks_;  // TODO: what class to use? Do I use a
  //                                   // thread pool? How to send messages?
  std::vector<Spinlock> per_thread_task_lock_{};
  std::vector<std::thread> threads_{};
  std::atomic<bool> stop_threads_{false};
};

inline ThreadPool::ThreadPool(const uint32_t number_of_threads)
    : stop_threads_{false}, threads_(number_of_threads) {
  for (uint32_t i = 0; i < number_of_threads; i++) {
    threads_.at(i) = std::thread(&ThreadPool::thread_loop, this, i);
    // TODO(nils): pin threads to CPUs with hwloc...
    // https://github.com/eliben/code-for-blog/blob/master/2016/threads-affinity/hwloc-example.cpp
  }
}

inline void ThreadPool::thread_loop(const uint32_t thread_id) {

  while (true) {
    // Use relaxed order since we don't care if we execute one more task or not.
    if (stop_threads_.load(std::memory_order_relaxed)) {
      return;
    }
  }
}

}  // namespace rts
