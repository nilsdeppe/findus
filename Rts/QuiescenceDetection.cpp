// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/QuiescenceDetection.hpp"

#include <numeric>
#include <ostream>
#include <vector>

namespace rts::qd {
bool Local::is_quiescent(const std::int64_t total_number_of_threads) {
  if (phase == 1) {
    if (number_of_idle_threads.load(std::memory_order_acquire) !=
        total_number_of_threads) {
      return false;
    }
    if (const std::int64_t message_count = number_of_messages_sent.load(
            std::memory_order::memory_order_acquire);
        // sends == receives
        message_count == number_of_messages_processed.load(
                             std::memory_order::memory_order_acquire)) {
      previous_count = message_count;
      phase = 2;
      return false;
    }
    // sends != receives
    return false;
  } else if (phase == 2) {
    if (number_of_idle_threads.load(std::memory_order_acquire) !=
        total_number_of_threads) {
      phase = 1;
      return false;
    }

    if (const std::int64_t message_count = number_of_messages_sent.load(
            std::memory_order::memory_order_acquire);
        // sends == receives
        message_count == number_of_messages_processed.load(
                             std::memory_order::memory_order_acquire)
        // AND previous_count == current_count
        and previous_count == message_count) {
      return true;
    }
    // sends != receives or (previous_count != current_count)
    //
    // Our previous hope for quiescence failed, go back to phase 1.
    phase = 1;
    return false;
  }
  throw std::runtime_error{
      "The only valid local quiescence detection phases are 1 and 2, but "
      "have the value " +
      std::to_string(phase)};
}

Global::Global() = default;
Global::Global(const Global&) = default;
Global& Global::operator=(const Global&) = default;
Global::Global(Global&&) = default;
Global& Global::operator=(Global&&) = default;
Global::~Global() = default;

namespace {
struct Tree {
  int self_rank{-1};
  int parent_rank{-1};
  int left_rank{-1};
  int right_rank{-1};
};

std::ostream& operator<<(std::ostream& os, const Tree& t) {
  return os << "[" << t.self_rank << ":p:" << t.parent_rank
            << ":l:" << t.left_rank
            << ":r:" << t.right_rank << "]";
}

/*!
 * \brief Compute the parent and children of the binary tree for the global QD
 * algorithm.
 *
 * We use a binary tree for simplicity and avoid creating the actual tree
 * since we can just have each rank know its parent and children.
 *
 * The sentinel value `-1` is used to represent a "no parent" and "no child".
 */
Tree parent_and_children(const int rank, const int total_ranks) {
  Tree result{rank};

  const int left_index = 2 * rank + 1;
  if (left_index < total_ranks) {
    result.left_rank = left_index;
  }
  const int right_index = 2 * rank + 2;
  if (right_index < total_ranks) {
    result.right_rank = right_index;
  }

  if (rank == 0) {
    result.parent_rank = -1;
  } else if (rank % 2 == 0) {
    result.parent_rank = (rank - 2) / 2;
  } else {
    result.parent_rank = (rank - 1) / 2;
  }
  return result;
}
}  // namespace

Global::Global(const int my_rank, const int total_ranks) {
  const Tree pnc = parent_and_children(my_rank, total_ranks);
  self_rank = pnc.self_rank;
  parent_rank = pnc.parent_rank;
  child_left_rank = pnc.left_rank;
  child_right_rank = pnc.right_rank;
}
}  // namespace rts::qd
