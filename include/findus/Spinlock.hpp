// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <atomic>

namespace findus {
/*!
 * \brief A simple spinlock implemented in `std::atomic`s
 *
 * Implementation basically from https://rigtorp.se/spinlock/
 */
class Spinlock {
 public:
  void lock() noexcept {
    for (;;) {
      // Optimistically assume the lock is free on the first try
      if (not lock_.exchange(true, std::memory_order_acquire)) {
        return;
      }
      // Wait for lock to be released without generating cache misses
      while (lock_.load(std::memory_order_relaxed)) {
        // Issue X86 PAUSE or ARM YIELD instruction to reduce contention between
        // hyper-threads
        //
        // If no hyperthreading is being used, this will actually slow down the
        // code.
        __builtin_ia32_pause();
      }
    }
  }

  bool try_lock() noexcept {
    // First do a relaxed load to check if lock is free in order to prevent
    // unnecessary cache misses if someone does while(!try_lock())
    return not lock_.load(std::memory_order_relaxed) and
           not lock_.exchange(true, std::memory_order_acquire);
  }

  void unlock() noexcept { lock_.store(false, std::memory_order_release); }

 private:
  std::atomic<bool> lock_{false};
};
}  // namespace findus
