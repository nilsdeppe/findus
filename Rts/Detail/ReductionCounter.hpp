// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "Rts/Exceptions/Exception.hpp"
#include "Rts/HardwareInfo.hpp"

namespace findus::reduction::detail {
/*!
 * \brief Tracks completion of reductions for a parallel component using unique
 * keys.
 *
 * The Counter class is used to track the total number of reductions
 * performed within a single parallel component. Each reduction is identified
 * by a unique 64-bit integer hashed key, which is used to avoid collisions
 * between different reductions.
 *
 * For each reduction, threads on the same process contribute locally. The
 * Counter tracks, via atomic counters, how many contributions have
 * been made for each reduction key. Once all expected contributions for a
 * given key have been made on a process, the reduction is considered complete
 * locally, and a further reduction across the thread-local reduced data can
 * be performed.
 *
 * The actual data being reduced is stored separately from the Counter.
 * However, the key used to access the reduced data must be the same as the
 * hashed key used in the Counter, or there must be a one-to-one and
 * onto mapping between them. This ensures that each reduction operation is
 * uniquely and correctly tracked.
 *
 * \note
 *   - The key value of 0 is reserved as a sentinel and cannot be used for
 *     reductions.
 *   - The class is designed for high concurrency and uses atomic operations
 *     to minimize synchronization overhead.
 *   - If the internal container is full and cannot insert a new key, an
 *     exception is thrown.
 *
 * <br>
 * \note
 *   The increment function of this class is thread-safe and intended to be used
 *   concurrently by multiple threads within a process. However, the
 *   constructor is not thread-safe.
 */
class alignas(
    findus::hardware_info::hardware_destructive_interference_size) Counter {
 public:
  /*!
   * \brief Because of the need for thread-safety, there is no useful case
   * where this class is default-constructed.
   */
  Counter() = delete;
  /*!
   * \brief Constructs a Counter with a specified maximum number of entries.
   *
   * This constructor initializes the internal container to track up to
   * `max_entries` unique reduction keys. Each entry is zero-initialized and
   * prepared for concurrent use by multiple threads.
   *
   * \param max_entries
   *   The maximum number of unique reduction keys that can be tracked.
   *   Must be a power of two greater than zero.
   *
   * \throws findus::Exception
   *   If `max_entries` is zero.
   *   If `max_entries` is not a power of two.
   *
   * \note
   *   The container size must be a power of two to ensure correct linear
   *   probing and efficient hashing. If this requirement is not met, an
   *   exception is thrown.
   */
  explicit Counter(size_t max_entries);
  // Delete copy and move constructors and assignment operators since this
  // class stores atomic variables needed for thread-safety.
  Counter(const Counter&) = delete;
  Counter& operator=(const Counter&) = delete;
  Counter(Counter&&) = delete;
  Counter& operator=(Counter&&) = delete;
  ~Counter();

  /*!
   * \brief Atomically increments the reduction counter for a given key and
   * checks if the reduction is complete across all process-local threads.
   *
   * This function coordinates parallel reduction operations across threads
   * on the same process. Each reduction is identified by a unique
   * `hashed_key`. The function tracks, via an inter-thread atomic counter,
   * how many contributions have been made for the given key.
   *
   * The argument `compute_expected` is a callable (e.g., a lambda) that
   * returns the number of contributions expected from the calling thread.
   * This allows reductions to be performed asynchronously and thread-locally
   * until all local contributions are ready, at which point the inter-thread
   * reduction is performed.
   *
   * \warning Each thread must provide a callable that provides the same number
   * of expected contributions for the same `hashed_key` on a process. This is
   * an unenforced contract with the user. The reason it is not enforced is to
   * ensure we invoke the callable only once per process per `hashed_key`,
   * eliminating extra cost.
   *
   * The function returns `true` if, after the current thread's contribution,
   * the reduction is complete across all local threads (i.e., all expected
   * contributions have been made). Only one thread will observe `true` for a
   * given reduction key.
   *
   * \tparam ComputeExpected
   *   A callable type that returns the number of expected contributions from
   *   the calling thread.
   * \param hashed_key
   *   The unique identifier for the reduction operation.
   * \param compute_expected
   *   A callable that returns the number of expected contributions from this
   *   thread.
   * \return
   *   `true` if the reduction is complete across all local threads, `false`
   *   otherwise.
   *
   * \note
   *   This function is thread-safe and is designed to be called concurrently
   *   from multiple threads. The internal counter and key are protected by
   *   atomic operations.
   *
   * <br>
   *
   * \note
   *   - The function uses atomic operations to minimize synchronization
   *     overhead.
   *   - The atomic counter is only used to track completion and synchronize
   *     access to the reduction data itself.
   *
   * \par Memory Ordering Caveats
   *   - The atomic operations on the key use `memory_order_acquire` and
   *     `memory_order_release` to ensure proper synchronization between threads
   *     when claiming or releasing a slot for a given key.
   *   - The initial increment of the counter (via `fetch_add`) uses
   *     `memory_order_relaxed` because only one thread is allowed to perform
   *     this operation for a given key, so no inter-thread synchronization is
   *     required at this point.
   *   - The decrement of the counter (via `fetch_sub`) uses
   *     `memory_order_release` to ensure that all memory writes performed by
   *     the thread before the decrement are visible to any thread that observes
   *     the counter reaching zero. This establishes a happens-before
   *     relationship between all memory changes prior to the `fetch_sub` and
   *     any operations after the counter reaches zero, on any thread.
   *   - When the counter reaches zero, the key is reset to zero using
   *     `memory_order_release` to ensure that all prior memory operations are
   *     visible before the slot is marked as free.
   *   - These memory orderings are chosen to minimize synchronization overhead
   *     while ensuring correctness and visibility of memory changes between
   *     threads at the critical points of slot acquisition, counter update, and
   *     slot release.
   *
   * \warning
   *   - The key value of 0 is reserved as a sentinel and cannot be used for
   *     reductions.
   *   - The function will throw if the container is full and cannot insert
   *     the key.
   */
  template <typename ComputeExpected>
  bool increment(std::uint64_t hashed_key,
                 const ComputeExpected& compute_expected);

 private:
  /*!
   * \brief Internal struct representing a single reduction key and its counter.
   *
   * Each KeyCount instance tracks a unique reduction key and the number of
   * contributions made for that key. The struct is aligned and padded to fill
   * an entire cache line, minimizing false sharing between threads.
   *
   * - `key`: An atomic 64-bit integer representing the reduction key.
   * - `counter`: An atomic 32-bit integer tracking the number of contributions
   *   for the associated key.
   * - `cacheline_fill`: Padding to ensure the struct occupies exactly one
   *   cache line, preventing false sharing.
   *
   * \note
   *   - The struct uses atomic variables for thread safety.
   *   - The cacheline size is determined by
   *     `hardware_info::hardware_destructive_interference_size`.
   */
  struct alignas(
      hardware_info::hardware_destructive_interference_size) KeyCount {
    std::atomic<std::uint64_t> key{0};
    std::atomic<std::int32_t> counter{0};
    static_assert(hardware_info::hardware_destructive_interference_size >= 12,
                  "The cacheline size is expected to be greater than or equal "
                  "to 12 bytes. This could be fixed by taking this into "
                  "account during the cacheline_fill size calculation.");
    std::array<std::byte,
               hardware_info::hardware_destructive_interference_size -
                   sizeof(std::atomic<std::uint64_t>) -
                   sizeof(std::atomic<std::uint32_t>)>
        cacheline_fill{};
  };
  static_assert(sizeof(KeyCount) ==
                    hardware_info::hardware_destructive_interference_size,
                "KeyCount is designed to be exactly the size of one cache line "
                "in order to avoid false sharing.");

  std::vector<KeyCount> entries_{};
};

template <typename ComputeExpected>
bool Counter::increment(const std::uint64_t hashed_key,
                        const ComputeExpected& compute_expected) {
  if (hashed_key == 0) {
    throw Exception{
        "The key value of 0 is not supported in reductions because it is "
        "used as a sentinel."};
  }
  for (std::uint64_t index = hashed_key; index != hashed_key - 1;
       ++index) {  // loop for linear probing
    index = index bitand (entries_.size() - 1);
    const std::uint64_t probed_key =
        entries_[index].key.load(std::memory_order_relaxed);
    if (probed_key != hashed_key) {
      if (probed_key != 0) {
        // The entry is used by another key.
        continue;
      }
      std::uint64_t current_key_in_slot = 0;
      if (not entries_[index].key.compare_exchange_strong(
              current_key_in_slot, hashed_key, std::memory_order_release,
              std::memory_order_acquire) and
          current_key_in_slot != hashed_key) {
        // Another thread just stole this slot from us. Try next slot.
        continue;
      }
      if (current_key_in_slot == 0) {
        // The entry was free, so we fetch_add to the counter.
        //
        // Note: it is crucial that we are guaranteeing that one and only
        // one thread calls fetch_add. If more than one thread could call
        // fetch_add then we would need memory_order_release to establish a
        // happens-before relationship with the fetch_sub.
        entries_[index].counter.fetch_add(compute_expected(),
                                          std::memory_order_relaxed);
      }
    }
    // Use memory_order_release to ensure that no operations before the
    // fetch_sub can be moved to after the fetch_sub. This establishes a
    // happens-before relationship between all memory changes prior to the
    // fetch_sub and any operations after the fetch_sub, _on any thread_.
    const int previous_counter =
        entries_[index].counter.fetch_sub(1, std::memory_order_release);
    if (previous_counter == 1) {
      entries_[index].key.store(0, std::memory_order_release);
      return true;
    }
    return false;
  }
  throw Exception{"Failed to insert hashed key " + std::to_string(hashed_key) +
                  " because the container is full. Max entries is " +
                  std::to_string(entries_.size())};
}
}  // namespace findus::reduction::detail
