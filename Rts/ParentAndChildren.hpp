// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <iosfwd>
#include <vector>

namespace rts::detail {
/// \brief Holds the info for the specific process ID's parent and children
/// process IDs.
struct ParentAndChildren {
  int self_process_id{-1};
  int parent_process_id{-1};
  int left_process_id{-1};
  int right_process_id{-1};
};

/// \brief Equivalence for ParentAndChildren
bool operator==(const ParentAndChildren& lhs, const ParentAndChildren& rhs);

/// \brief Inequivalence for ParentAndChildren
bool operator!=(const ParentAndChildren& lhs, const ParentAndChildren& rhs);

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

/*!
 * \brief Returns all children (descendants) in the subtree of a given process.
 *
 * Given a process ID and the total number of process IDs, this function
 * computes all children and descendants of the specified process in a binary
 * tree arrangement. The result is a vector of process IDs that are in the
 * subtree rooted at the given process (excluding the process itself).
 *
 * The binary tree is constructed such that for process ID `i`:
 * - Left child: `2 * i + 1` (if less than total_process_ids)
 * - Right child: `2 * i + 2` (if less than total_process_ids)
 *
 * The function throws an Exception if the process ID or `total_process_ids`
 * are negative, or if the process ID is out of bounds.
 *
 * \param process_id The process ID whose subtree children are to be computed.
 * \param total_process_ids The total number of process IDs in the system.
 * \return A vector of all descendant process IDs in the subtree.
 * \throws Exception if arguments are invalid.
 */
std::vector<int> children_in_subtree(int process_id, int total_process_ids);
}  // namespace rts::detail
