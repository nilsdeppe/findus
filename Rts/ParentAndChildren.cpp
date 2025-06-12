// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/ParentAndChildren.hpp"

#include <ostream>
#include <string>

namespace rts::detail {
std::ostream& operator<<(std::ostream& os, const ParentAndChildren& t) {
  return os << "[" << t.self_process_id << ":p:" << t.parent_process_id
            << ":l:" << t.left_process_id << ":r:" << t.right_process_id << "]";
}

ParentAndChildren parent_and_children(const int process_id,
                                      const int total_process_ides) {
  ParentAndChildren result{process_id};

  const int left_index = 2 * process_id + 1;
  if (left_index < total_process_ides) {
    result.left_process_id = left_index;
  }
  const int right_index = 2 * process_id + 2;
  if (right_index < total_process_ides) {
    result.right_process_id = right_index;
  }

  if (process_id == 0) {
    result.parent_process_id = -1;
  } else if (process_id % 2 == 0) {
    result.parent_process_id = (process_id - 2) / 2;
  } else {
    result.parent_process_id = (process_id - 1) / 2;
  }
  return result;
}
}  // namespace rts::detail
