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

template <class Action, class Component, class... Args>
struct DummyCallback {
  int value;
  DummyCallback(int v) : value(v) {}
  bool operator==(const DummyCallback& other) const {
    return value == other.value;
  }
};

void test_combine_function() {
  using namespace rts::reduction;
  using DataTuple = std::tuple<int, double>;

  // Simple binary op: sum for int, product for double
  struct SumProductOp {
    void operator()(DataTuple& lhs, const int rhs_int,
                    const double rhs_double) const {
      std::get<0>(lhs) += rhs_int;
      std::get<1>(lhs) *= rhs_double;
    }
  };

  // Prepare two messages with the same reduction id
  const std::uint32_t distributed_object_index = 1;
  const std::uint64_t reduction_id = 1234;
  DataTuple data0{2, 3.0};
  DataTuple data1{5, 4.0};

  // Dummy callback (not used in combine)
  struct DummyAction {};
  struct DummyComponent {};
  using CallbackType = DummyCallback<DummyAction, DummyComponent, int, double>;
  CallbackType callback{0};

  // Create two messages
  Message_t msg0 = create_message(distributed_object_index, reduction_id, data0,
                                  callback, MessageType::Reduction);
  Message_t msg1 = create_message(distributed_object_index, reduction_id, data1,
                                  callback, MessageType::Reduction);

  // Combine msg1 into msg0
  detail::combine<SumProductOp, DataTuple>(msg0, msg1);

  // Check result: (2+5, 3.0*4.0) = (7, 12.0)
  const DataTuple* result =
      rts::data_from_message<DataTuple>(*msg0.get_header());
  CHECK(std::get<0>(*result) == 7);
  CHECK(std::get<1>(*result) == 12.0);

  // Test error: mismatched reduction id
  Message_t msg2 = create_message(distributed_object_index, reduction_id + 1,
                                  data1, callback, MessageType::Reduction);
  CHECK_THROWS_WITH_AS((detail::combine<SumProductOp, DataTuple>(msg0, msg2)),
                       "The reduction id in the two reduction messages must "
                       "match but message0 has: 1234 and message1 has: 1235",
                       rts::Exception);
}
}  // namespace
}  // namespace rts::reduction

TEST_CASE("Reduction") {
  rts::reduction::test_insert_action_stream_operator();
  rts::reduction::test_combine_function();
}

// #endif
