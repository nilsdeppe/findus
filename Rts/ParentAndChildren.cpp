// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/ParentAndChildren.hpp"

#include <ostream>
#include <string>
#include <vector>

#include "Rts/Exceptions/Exception.hpp"

namespace rts::detail {
bool operator==(const ParentAndChildren& lhs, const ParentAndChildren& rhs) {
  return lhs.self_process_id == rhs.self_process_id and
         lhs.parent_process_id == rhs.parent_process_id and
         lhs.left_process_id == rhs.left_process_id and
         lhs.right_process_id == rhs.right_process_id;
}

bool operator!=(const ParentAndChildren& lhs, const ParentAndChildren& rhs) {
  return not(lhs == rhs);
}

std::ostream& operator<<(std::ostream& os, const ParentAndChildren& t) {
  return os << "[" << t.self_process_id << ":p:" << t.parent_process_id
            << ":l:" << t.left_process_id << ":r:" << t.right_process_id << "]";
}

ParentAndChildren parent_and_children(const int process_id,
                                      const int total_process_ids) {
  if (process_id < 0) {
    throw Exception{"Process ID must be non-negative but got " +
                    std::to_string(process_id)};
  }
  if (total_process_ids < 0) {
    throw Exception{"Total process IDs must be non-negative but got " +
                    std::to_string(total_process_ids)};
  }
  if (process_id + 1 > total_process_ids) {
    throw Exception{
        "Process ID must be less than or equal to total_process_ids-1, but "
        "process_id is " +
        std::to_string(process_id) + " and total_process_ids is " +
        std::to_string(total_process_ids)};
  }

  ParentAndChildren result{process_id};

  const int left_index = 2 * process_id + 1;
  if (left_index < total_process_ids) {
    result.left_process_id = left_index;
  }
  const int right_index = 2 * process_id + 2;
  if (right_index < total_process_ids) {
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

std::vector<int> children_in_subtree(const int process_id,
                                     const int total_process_ids) {
  if (process_id < 0) {
    throw Exception{"Process ID must be non-negative but got " +
                    std::to_string(process_id)};
  }
  if (total_process_ids < 0) {
    throw Exception{"Total process IDs must be non-negative but got " +
                    std::to_string(total_process_ids)};
  }
  if (process_id + 1 > total_process_ids) {
    throw Exception{
        "Process ID must be less than or equal to total_process_ids-1, but "
        "process_id is " +
        std::to_string(process_id) + " and total_process_ids is " +
        std::to_string(total_process_ids)};
  }
  std::vector<int> result{};
  result.reserve(static_cast<size_t>(total_process_ids));
  size_t start = 0;
  {
    const auto pc = parent_and_children(process_id, total_process_ids);
    if (pc.left_process_id != -1) {
      result.push_back(pc.left_process_id);
    }
    if (pc.right_process_id != -1) {
      result.push_back(pc.right_process_id);
    }
  }
  size_t end = result.size();
  while (true) {
    bool some_added = false;
    for (size_t i = start; i < end; ++i) {
      const auto pc = parent_and_children(result[i], total_process_ids);
      if (pc.left_process_id != -1) {
        result.push_back(pc.left_process_id);
        some_added = true;
      }
      if (pc.right_process_id != -1) {
        result.push_back(pc.right_process_id);
        some_added = true;
      }
    }
    if (not some_added) {
      break;
    }
    start = end;
    end = result.size();
  }
  result.shrink_to_fit();
  return result;
}
}  // namespace rts::detail

#if defined(RTS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <string>

#include "Rts/Detail/GetOutput.hpp"
#include "Rts/Detail/VectorStream.hpp"

namespace rts::detail {

namespace {
void test_p_and_c() {
  CHECK(get_output(ParentAndChildren{1, 2, 3, 4}) == "[1:p:2:l:3:r:4]");
  CHECK_THROWS_AS(parent_and_children(-5, 8), Exception);
  try {
    parent_and_children(-5, 8);
  } catch (const std::runtime_error& e) {
    CHECK(std::string(e.what()) ==
          "Process ID must be non-negative but got -5");
  }
  CHECK_THROWS_AS(parent_and_children(5, -1), Exception);
  try {
    parent_and_children(5, -1);
  } catch (const std::runtime_error& e) {
    CHECK(std::string(e.what()) ==
          "Total process IDs must be non-negative but got -1");
  }
  CHECK_THROWS_AS(parent_and_children(5, 3), Exception);
  try {
    parent_and_children(5, 3);
  } catch (const std::runtime_error& e) {
    CHECK(std::string(e.what()) ==
          "Process ID must be less than or equal to total_process_ids-1, but "
          "process_id is 5 and total_process_ids is 3");
  }
  for (int i = 0; i < 5; ++i) {
    CHECK_NOTHROW(parent_and_children(i, 5));
  }

  const ParentAndChildren expected{1, 2, 3, 4};
  CHECK(expected != ParentAndChildren{9, 2, 3, 4});
  CHECK(expected != ParentAndChildren{1, 9, 3, 4});
  CHECK(expected != ParentAndChildren{1, 2, 9, 4});
  CHECK(expected != ParentAndChildren{1, 2, 3, 9});

  CHECK(parent_and_children(0, 1) == ParentAndChildren{0});

  CHECK(parent_and_children(0, 2) == ParentAndChildren{0, -1, 1});
  CHECK(parent_and_children(1, 2) == ParentAndChildren{1, 0});

  CHECK(parent_and_children(0, 3) == ParentAndChildren{0, -1, 1, 2});
  CHECK(parent_and_children(1, 3) == ParentAndChildren{1, 0, -1, -1});
  CHECK(parent_and_children(2, 3) == ParentAndChildren{2, 0, -1, -1});

  CHECK(parent_and_children(0, 7) == ParentAndChildren{0, -1, 1, 2});
  CHECK(parent_and_children(1, 7) == ParentAndChildren{1, 0, 3, 4});
  CHECK(parent_and_children(2, 7) == ParentAndChildren{2, 0, 5, 6});
  CHECK(parent_and_children(3, 7) == ParentAndChildren{3, 1, -1, -1});
  CHECK(parent_and_children(4, 7) == ParentAndChildren{4, 1, -1, -1});
  CHECK(parent_and_children(5, 7) == ParentAndChildren{5, 2, -1, -1});
  CHECK(parent_and_children(6, 7) == ParentAndChildren{6, 2, -1, -1});

  CHECK(parent_and_children(0, 12) == ParentAndChildren{0, -1, 1, 2});
  CHECK(parent_and_children(1, 12) == ParentAndChildren{1, 0, 3, 4});
  CHECK(parent_and_children(2, 12) == ParentAndChildren{2, 0, 5, 6});
  CHECK(parent_and_children(3, 12) == ParentAndChildren{3, 1, 7, 8});
  CHECK(parent_and_children(4, 12) == ParentAndChildren{4, 1, 9, 10});
  CHECK(parent_and_children(5, 12) == ParentAndChildren{5, 2, 11, -1});
  CHECK(parent_and_children(6, 12) == ParentAndChildren{6, 2, -1, -1});
  CHECK(parent_and_children(7, 12) == ParentAndChildren{7, 3, -1, -1});
  CHECK(parent_and_children(8, 12) == ParentAndChildren{8, 3, -1, -1});
  CHECK(parent_and_children(9, 12) == ParentAndChildren{9, 4, -1, -1});
  CHECK(parent_and_children(10, 12) == ParentAndChildren{10, 4, -1, -1});
  CHECK(parent_and_children(11, 12) == ParentAndChildren{11, 5, -1, -1});
}

void test_subtree() {
  using rts::detail::children_in_subtree;
  using rts::detail::operator<<;

  // Edge cases: invalid arguments
  CHECK_THROWS_AS(children_in_subtree(-1, 5), Exception);
  CHECK_THROWS_AS(children_in_subtree(2, -1), Exception);
  CHECK_THROWS_AS(children_in_subtree(5, 3), Exception);

  // Single process: no children
  CHECK(children_in_subtree(0, 1).empty());

  // Two processes: root has one child
  {
    std::vector<int> expected{1};
    CHECK(children_in_subtree(0, 2) == expected);
    CHECK(children_in_subtree(1, 2).empty());
  }

  // Three processes: root has two children, children have none
  {
    std::vector<int> expected{1, 2};
    CHECK(children_in_subtree(0, 3) == expected);
    CHECK(children_in_subtree(1, 3).empty());
    CHECK(children_in_subtree(2, 3).empty());
  }

  // Seven processes: test subtree for each node
  {
    // Tree: 0->1,2; 1->3,4; 2->5,6
    CHECK(children_in_subtree(0, 7) == std::vector<int>{1, 2, 3, 4, 5, 6});
    CHECK(children_in_subtree(1, 7) == std::vector<int>{3, 4});
    CHECK(children_in_subtree(2, 7) == std::vector<int>{5, 6});
    CHECK(children_in_subtree(3, 7).empty());
    CHECK(children_in_subtree(4, 7).empty());
    CHECK(children_in_subtree(5, 7).empty());
    CHECK(children_in_subtree(6, 7).empty());
  }

  // Larger tree: 12 processes
  {
    CHECK(children_in_subtree(0, 12) ==
          std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
    CHECK(children_in_subtree(1, 12) == std::vector<int>{3, 4, 7, 8, 9, 10});
    CHECK(children_in_subtree(2, 12) == std::vector<int>{5, 6, 11});
    CHECK(children_in_subtree(3, 12) == std::vector<int>{7, 8});
    CHECK(children_in_subtree(4, 12) == std::vector<int>{9, 10});
    CHECK(children_in_subtree(5, 12) == std::vector<int>{11});
    CHECK(children_in_subtree(6, 12).empty());
    CHECK(children_in_subtree(7, 12).empty());
    CHECK(children_in_subtree(8, 12).empty());
    CHECK(children_in_subtree(9, 12).empty());
    CHECK(children_in_subtree(10, 12).empty());
    CHECK(children_in_subtree(11, 12).empty());
  }
}

void test_count_first_descendants() {
  const int total_layers = 8;
  // For a complete binary tree, total nodes = 2^layers - 1
  const int total_processes = (1 << total_layers) - 1;

  // Start from a node 3 layers down (layer 3, 0-based)
  // Layer 0:           root (0)
  // Layer 1:       1,            2
  // Layer 2:   3,    4,      5,      6
  // Layer 3: 7, 8, 9, 10, 11, 12, 13, 14
  // Subtree below node 3:
  // Layer 4: 15, 16 (children of 7), 17, 18 (children of 8)
  // Layer 5: 31, 32 (children of 15), 33, 34 (children of 16),
  //          35, 36 (children of 17), 37, 38 (children of 18)
  //        = 31 to 38
  // Layer 6: 63, 64 (children of 31), 65, 66 (children of 32),
  //          67, 68 (children of 33), 69, 70 (children of 34),
  //          71, 72 (children of 35), 73, 74 (children of 36),
  //          75, 76 (children of 37), 77, 78 (children of 38)
  //        = 63 to 78
  // Layer 7: 127, 128 (children of 63), 129, 130 (children of 64),
  //          131, 132 (children of 65), 133, 134 (children of 66),
  //          135, 136 (children of 67), 137, 138 (children of 68),
  //          139, 140 (children of 69), 141, 142 (children of 70),
  //          143, 144 (children of 71), 145, 146 (children of 72),
  //          147, 148 (children of 73), 149, 150 (children of 74),
  //          151, 152 (children of 75), 153, 154 (children of 76),
  //          155, 156 (children of 77), 157, 158 (children of 78)
  //        = 127 to 158
  //
  // - The node numbers for each layer can be calculated as:
  // - Layer 4: children of 7–14 (nodes 15–30)
  // - Layer 5: children of 15–30 (nodes 31–62)
  // - Layer 6: children of 31–62 (nodes 63–126)
  // - Layer 7: children of 63–126 (nodes 127–254)
  //
  // Let's pick node 3 (layer 2) as the starting node
  const int process_id = 3;

  {
    // Compute expected number of leaf descendants under node 3
    // Subtree rooted at node 3 is a complete binary tree of depth 5 (layers
    // 3-7) Number of leaves in a complete binary tree of depth d: 2^(d-1) Here,
    // d = 6 (layers 2 to 7 inclusive), so leaves = 2^(6-1) = 32
    const int expected_leaves = 32;

    // Predicate: only leaf nodes contribute
    const auto is_leaf = [total_processes](const int pid) -> bool {
      (void)total_processes;
      const rts::detail::ParentAndChildren pc =
          rts::detail::parent_and_children(pid, total_processes);
      return pc.left_process_id == -1 and pc.right_process_id == -1;
    };

    const int count =
        count_first_descendants(process_id, total_processes, is_leaf);
    CHECK(count == expected_leaves);
  }

  {
    // Additional test: mark every even process_id as contributing
    const auto even_predicate = [](const int pid) -> bool {
      return pid % 2 == 0;
    };
    // For a complete binary tree, starting at 3, count all even
    // descendants
    //
    // The nodes are: 8, 16, 32, 64, 128
    const int even_count =
        count_first_descendants(process_id, total_processes, even_predicate);
    CHECK(even_count == 5);
  }

  {
    // Additional test: mark every even process_id as contributing
    const auto even_or_parent_is_63_or_65_predicate =
        [](const int pid) -> bool {
      return pid % 2 == 0 or
             (parent_and_children(pid, total_processes).parent_process_id ==
                  63 or
              parent_and_children(pid, total_processes).parent_process_id ==
                  65);
    };
    // For a complete binary tree, starting at 3, count all even
    // descendants
    //
    // The nodes are: 8, 16, 32, 64, 127, 128
    const int even_count = count_first_descendants(
        process_id, total_processes, even_or_parent_is_63_or_65_predicate);
    CHECK(even_count == 6);
  }

  {
    // Another test: all nodes contribute
    const auto all_predicate = [](const int /*unused*/) -> bool {
      return true;
    };
    const int all_count =
        count_first_descendants(0, total_processes, all_predicate);
    // Only the immediate children of root (1 and 2) are counted
    CHECK(all_count == 2);
  }

  {
    // Another test: no nodes contribute
    const auto none_predicate = [](const int /*unused*/) -> bool {
      return false;
    };
    const int none_count =
        count_first_descendants(0, total_processes, none_predicate);
    CHECK(none_count == 0);
  }
}
}  // namespace

TEST_CASE("ParentAndChildren") {
  test_p_and_c();
  test_subtree();
  test_count_first_descendants();
}
}  // namespace rts::detail
#endif
