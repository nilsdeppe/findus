// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <iosfwd>
#include <stack>
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

/*!
 * \brief Counts the number of "first descendant" nodes in a binary tree
 *        that satisfy a given predicate.
 *
 * Starting from a specified node (given by \p process_id), this function
 * traverses the binary tree defined by process IDs and parent-child
 * relationships. For each branch (left and right), it searches for the
 * first descendant node that satisfies the provided predicate \p predicate.
 * Once a node in a branch satisfies the predicate, it is counted and
 * traversal does not continue further down that branch.
 *
 * The root node (\p process_id) itself is never checked against the predicate.
 *
 * \tparam Predicate
 *   A callable type with signature <tt>bool(int)</tt> that returns true if
 *   the node should be counted.
 *
 * \param process_id
 *   The integer ID of the node from which to start the search (the root of
 *   the subtree).
 * \param total_processes
 *   The total number of nodes (processes) in the tree.
 * \param predicate
 *   A callable that takes an integer process ID and returns true if the node
 *   should be counted as a "first descendant."
 *
 * \return
 *   The total number of first descendant nodes (across all branches) that
 *   satisfy the predicate.
 *
 * \details
 *   - The function does not check the root node itself.
 *   - For each branch, only the first node that satisfies the predicate is
 *     counted; its descendants are not checked.
 *   - If a branch contains no node that satisfies the predicate, it
 *     contributes zero to the count.
 *   - The tree structure and parent-child relationships are determined by
 *     \ref rts::detail::parent_and_children.
 *
 * \see rts::detail::parent_and_children
 */
template <class Predicate>
int count_first_descendants(const int process_id, const int total_processes,
                            const Predicate& predicate) {
  int count = 0;
  const rts::detail::ParentAndChildren pc =
      rts::detail::parent_and_children(process_id, total_processes);

  std::stack<int> stack;
  // Start with the immediate children (left and right)
  if (pc.left_process_id != -1) {
    stack.push(pc.left_process_id);
  }
  if (pc.right_process_id != -1) {
    stack.push(pc.right_process_id);
  }

  while (not stack.empty()) {
    const int current_process_id = stack.top();
    stack.pop();

    if (predicate(current_process_id)) {
      count += 1;
      // Do not push this node's children; stop this branch
      continue;
    }

    const rts::detail::ParentAndChildren child_pc =
        rts::detail::parent_and_children(current_process_id, total_processes);
    if (child_pc.left_process_id != -1) {
      stack.push(child_pc.left_process_id);
    }
    if (child_pc.right_process_id != -1) {
      stack.push(child_pc.right_process_id);
    }
  }

  return count;
}
}  // namespace rts::detail
