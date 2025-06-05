// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>

namespace rts {
/// Quiescence detection constructs
namespace qd {
namespace detail {
#ifdef __cpp_lib_hardware_interference_size
static constexpr std::size_t hardware_destructive_interference_size =
    std::hardware_destructive_interference_size;
#else
static constexpr std::size_t hardware_destructive_interference_size = 64;
#endif
}  // namespace detail

/*!
 * \brief The data needed for local quiescence detection within an MPI rank.
 *
 * This is used for implementing the SKR algorithm used by Charm++ and
 * described in:
 *  Authors: Amitabh B. Sinha, Laxmikant Kale, Balkrishna Ramkumar
 *  Title: A dynamic and adaptive quiescence detection algorithm
 *  Year: 1993
 *
 * In an intranode setting we can use atomics to count the number of threads
 * that are quiescent. Since a large number of threads may be available on a
 * node, we want to minimize contention (and cache misses) of the
 * atomics. Note that incrementing an integer is a FetchAdd operation and so
 * is a more expensive atomic operation.  There are several techniques we
 * can combine to reduce the contention on the atomics:
 * 1. The driver thread only checks for local quiescence after $L_0$
 *    iterations with no incoming and outgoing messages. If we have any
 *    incoming or outgoing messages, we are guaranteed to have active threads.
 * 2. The threads only mark themselves as passive after $L_1$ attempts to
 *    retrieve a new task to work on without success.
 * 3. Each atomic counter has at most $L_3$ threads that may write to it.
 * 4. Make sure atomics are cache-line aligned and take up the entire cache
 *    line. This prevents false sharing among other pieces of data, in
 *    particular between atomics across different groups of threads.
 *
 * Within a node we effectively use the SKR algorithm. Whenever a message is
 * sent, we increment the counter `local_number_of_messages_sent` and whenever
 * a message is done being processed we increment the counter
 * `local_number_of_messages_processed`. Since these are atomic counters,
 * the same performance considerations apply as above. When we determine
 * that all threads are passive we record the number of messages sent and
 * processed. We then store a copy of the values before checking for
 * incoming and outgoing MPI messages. If we have any incoming or outgoing
 * MPI messages, then we haven't reached quiescence and we go back to
 * "Phase 1" of SKR.
 */
class Local {
 public:
  /// Increment the number of idle threads
  void increment_idle_thread_count() {
    number_of_idle_threads.fetch_add(1,
                                     std::memory_order::memory_order_acq_rel);
  }

  /// Decrement the number of idle threads
  void decrement_idle_thread_count() {
    number_of_idle_threads.fetch_sub(1,
                                     std::memory_order::memory_order_acq_rel);
  }

  /// Increment the number of messages sent
  void increment_sent() {
    number_of_messages_sent.fetch_add(1,
                                      std::memory_order::memory_order_acq_rel);
  }

  /// Increment the number of messages processed
  void increment_processed() {
    number_of_messages_processed.fetch_add(
        1, std::memory_order::memory_order_acq_rel);
  }

  /// Applies the SKR check for quiescence. Returns `true` if the system is
  /// quiescent and `false` if not.
  ///
  /// The local data does not track the total number of threads, only the
  /// number of threads that have reported idle, so the function takes as
  /// argument the total number of threads.
  ///
  /// \warning This call is not threadsafe on the member variables
  /// `previous_count` and `phase`!
  bool is_quiescent(std::int64_t total_number_of_threads);

 private:
  /// The phase of the SKR algorithm we are in.
  ///
  /// Valid states are 1 and 2.
  std::uint8_t phase{1};
  /// In the SKR algorithm the number_of_idle_threads being equal to the
  /// number of threads means all threads have sent their idle status.
  alignas(detail::hardware_destructive_interference_size)
      std::atomic<std::int64_t> number_of_idle_threads{0};
  /// number_of_messages_sent is N_c in the SKR algorithm.
  alignas(detail::hardware_destructive_interference_size)
      std::atomic<std::int64_t> number_of_messages_sent{0};
  /// number_of_messages_processed is N_p in the SKR algorithm.
  alignas(detail::hardware_destructive_interference_size)
      std::atomic<std::int64_t> number_of_messages_processed{0};
  /// previous_count is N_p in the SKR algorithm.
  alignas(detail::hardware_destructive_interference_size) std::int64_t
      previous_count{0};
};

/*!
 * \brief The data needed for global quiescence detection across all MPI
 * ranks.
 *
 * The algorithm implemented for global quiescence detection is the BCJ
 * algorithm of:
 *   Authors: Allison H. Baker, Silvia Crivelli, E. R. Jessup
 *   Title: "An efficient parallel termination detection algorithm"
 *   Year: 2008
 *   Journal: International Journal of Parallel Emergent and Distributed
 *            Systems
 *   DOI: 10.1080/00036810600595813
 *
 * In BJC a sweep counter is used to keep track of different quiescence
 * detection attempts. A virtual spanning tree is used across the MPI ranks
 * where each rank has $N$ children. Ranks with no children are referred to
 * leaf nodes in the tree and the single rank with no parent is called the
 * root. The algorithm has a down traversal that starts at the root node and
 * an up traversal that starts at the leaf nodes. A new down traversal is only
 * started after an up traversal completes. Note that down and up traversals
 * carry different pieces of information. The message structure is
 *
 * ```cpp
 * struct BjcMessage {
 *   // We use the largest bit of `sweep_number` to signal a down or up sweep.
 *   // `0` means down while `1` means up.
 *   std::uint64_t sweep_number;         // Both down & up.
 *   std::int64_t sends_minus_receives;  // Only up
 * };
 * ```
 *
 * The algorithm starts with local quiescence monitoring on the root
 * node. When the root node is locally quiescent, the first down traversal is
 * started.
 *
 * The below pseudocode illustrates the down traversal algorithm.
 * ```cpp
 * void down_traversal() {
 *   if (not root) {
 *     // Check for a down message from the parent. We keep a local temporary
 *     // record of this sweep number.
 *     sweep_number = receive_down_message_from_parent();
 *   }
 *
 *   if (not leaf) {
 *     send_down_message_to_children(sweep_number, children);
 *   } else { // We are a leaf
 *     // We now initiate the up traversal
 *     local_send_minus_receive = local_send - local_receive;
 *     send_up_message(sweep_number, parent, local_send_minus_receive);
 *   }
 * }
 * ```
 *
 * The up traversal is initiated by each leaf node when it has received a down
 * message. The up messages contain the sweep number and the total number
 * sends minus receives from all children. The following pseudocode
 * ellistrates the up algorithm:
 * ```cpp
 * bool up_traversal() {
 *   if (leaf) { return; }
 *
 *   optional<message> = check_up_message_from_children();
 *   if (not message) { return; }
 *
 *   children_accumulated_count[message->sweep_number] =
 *       safe_add(children_accumulated_count[message->sweep_number],
 *                message->count);
 *   children_received[message->sweep_number]++;
 *
 *   if (children_received[message->sweep_number] != num_children) { return; }
 *
 *   // here local_sweep_number is the sweep number of the _previous_ sweep
 *   if (last_regular_message_sweep_number > local_sweep_number) {
 *     local_count = numeric_limits<int64_t>::max();
 *   } else {
 *     local_count = local_send - local_receive;
 *   }
 *   count = safe_add(local_count,
 *                    children_accumulated_count[message->sweep_number]);
 *   if (not root) {
 *      send_up_to_parent(count, parent);
 *      local_sweep_number = message->sweep_number
 *   } else { // Is root
 *     if (count == 0) {
 *       broadcast_termination();
 *       return true; // quiescence is detected
 *     } else {
 *       down_traversal();
 *     }
 *   }
     return false;
 * }
 * ```
 *
 * The function `safe_add` behaves as
 * ```cpp
 * int64_t safe_add(const int64_t a, const int64_t b) {
 *   return (a == numeric_limits<int64_t>::max() or
 *           b == numeric_limits<int64_t>::max()) ?
 *          numeric_limits<int64_t>::max() : (a + b);
 * }
 * ```
 *
 * Note that the root node starts a new sweep whenever it is passive, which
 * means multiple sweeps could be simultaneously in progress. This is by
 * design and allows for termination as early as possible.
 */
struct Global {
  struct Message {
    std::uint64_t direction_and_sweep_number{0};
    std::int64_t count{0};

    static Message create(const bool direction_is_up,
                          const std::uint64_t sweep_number,
                          const std::int64_t in_count) {
      Message message{sweep_number, in_count};
      if (direction_is_up) {
        message.direction_and_sweep_number =
            message.direction_and_sweep_number bitor
            (static_cast<std::uint64_t>(0b1) << 63);
      }
      return message;
    }

    static bool up_traversal(const Message& message) {
      return static_cast<bool>(message.direction_and_sweep_number bitand
                               (static_cast<std::uint64_t>(0b1) << 63));
    }

    static bool down_traversal(const Message& message) {
      return not up_traversal(message);
    }
  };

  Global();
  Global(const Global&);
  Global& operator=(const Global&);
  Global(Global&&);
  Global& operator=(Global&&);
  ~Global();

  Global(int my_rank, int total_ranks);

  std::uint64_t local_sweep_number{0};
  std::uint64_t last_regular_message_sweep_number{0};
  std::int64_t local_sends{0};
  std::int64_t local_processed{0};
  // The sentinel value of `-1` denotes "not filled" while the sentinel value
  // `std::numeric_limits<int>::min()` denotes "not initialized".
  int self_rank{std::numeric_limits<int>::min()};
  int parent_rank{std::numeric_limits<int>::min()};
  int child_left_rank{std::numeric_limits<int>::min()};
  int child_right_rank{std::numeric_limits<int>::min()};
};
}  // namespace qd
}  // namespace rts
