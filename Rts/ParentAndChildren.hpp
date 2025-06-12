// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <iosfwd>

namespace rts::detail {
/// \brief Holds the info for the specific process ID's parent and children
/// process IDs.
struct ParentAndChildren {
  int self_process_id{-1};
  int parent_process_id{-1};
  int left_process_id{-1};
  int right_process_id{-1};
};

/// \brief Stream operator for `ParentAndChildren`
std::ostream& operator<<(std::ostream& os, const ParentAndChildren& t);

/*!
 * \brief Compute the parent and children of the process with `processed_id`.
 *
 * We use a binary tree for simplicity and avoid creating the actual tree
 * since we can just have each process know its parent and children.
 *
 * The sentinel value `-1` is used to represent a "no parent" and "no child".
 */
ParentAndChildren parent_and_children(int process_id, int total_processes);
}  // namespace rts::detail
