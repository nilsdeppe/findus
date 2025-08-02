// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/Reduction.hpp"

#include <cstddef>
#include <cstdint>

namespace rts::reduction {
std::ostream& operator<<(std::ostream& os, const InsertAction action) {
  switch (action) {
    case InsertAction::Insert:
      return os << "Insert";
    case InsertAction::Combine:
      return os << "Combine";
    case InsertAction::Complete:
      return os << "Complete";
    default:
      return os << "Unknown";
  }
}
}  // namespace rts::reduction

// #if defined(RTS_ENABLE_TESTING)

#include <doctest/doctest.h>

#include "Rts/Detail/GetOutput.hpp"
#include "Rts/Message.hpp"
#include "Rts/Reduction.hpp"

namespace rts::reduction {
namespace {
void test_insert_action_stream_operator() {
  using rts::detail::get_output;

  CHECK(get_output(InsertAction::Insert) == "Insert");
  CHECK(get_output(InsertAction::Combine) == "Combine");
  CHECK(get_output(InsertAction::Complete) == "Complete");
}
}  // namespace
}  // namespace rts::reduction

TEST_CASE("InsertAction") {
  rts::reduction::test_insert_action_stream_operator();
}

// #endif
