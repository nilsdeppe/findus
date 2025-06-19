// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/ParentAndChildren.hpp"

#include <ostream>
#include <string>

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
}  // namespace rts::detail

#if defined(RTS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <string>

#include "Rts/Detail/GetOutput.hpp"

namespace rts::detail {

TEST_CASE("ParentAndChildren") {
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
}  // namespace rts::detail
#endif
