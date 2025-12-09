// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "findus/Reduction.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "findus/Detail/ActiveObject.hpp"
#include "findus/Detail/GetOutput.hpp"
#include "findus/Exceptions/Exception.hpp"
#include "findus/MessageType.hpp"
#include "findus/Serialize/Serializer.hpp"

namespace findus::reduction {
std::ostream& operator<<(std::ostream& os, const InsertAction action) {
  switch (action) {
    case InsertAction::Insert:
      return os << "Insert";
    case InsertAction::Combine:
      return os << "Combine";
    case InsertAction::Complete:
      return os << "Complete";
    case InsertAction::AtCapacity:
      return os << "AtCapacity";
    default:
      return os << "Unknown";
  }
}

DataHandler::DataHandler(const size_t max_simultaneous_reductions)
    : entries_(max_simultaneous_reductions) {}

Message_t DataHandler::pop(const std::uint64_t reduction_id) {
  const std::optional<std::uint64_t> index = index_of(reduction_id);
  if (index.has_value()) {
    Message_t t = std::move(entries_[reduction_id].callback_and_data);
    // Synchronizes with the insert_or_combine() operation. We need to make
    // sure the move out of the entry happens-before the clearing of the slot.
    entries_[index.value()].reduction_id.store(0, std::memory_order_release);
    return t;
  } else {
    throw Exception{"Could not find reduction ID " +
                    std::to_string(reduction_id)};
  }
}

std::optional<std::uint64_t> DataHandler::index_of(
    const std::uint64_t reduction_id) const {
  for (std::uint64_t index = reduction_id, counter = 0;
       counter < entries_.size();
       (void)++index, (void)++counter) {  // loop for linear probing
    index = index bitand (entries_.size() - 1);
    // This does not synchronize
    const std::uint64_t probed_reduction_id =
        entries_[index].reduction_id.load(std::memory_order_relaxed);
    if (probed_reduction_id == reduction_id) {
      return index;
    }
  }
  return std::nullopt;
}

size_t DataHandler::capacity() const { return entries_.size(); }

Handler::Handler(const size_t number_of_threads,
                 const size_t max_simultaneous_reductions)
    : reduction_counter_{max_simultaneous_reductions},
      inter_process_entries_(max_simultaneous_reductions) {
  per_thread_data_handlers_.reserve(number_of_threads);
  for (size_t i = 0; i < number_of_threads; ++i) {
    per_thread_data_handlers_.emplace_back(max_simultaneous_reductions);
  }
}

std::optional<Message_t> Handler::combine_inter_process(
    Message_t message, const findus::detail::ParentAndChildren p_and_c) {
  if (message.get_header()->message_type() != MessageType::Reduction and
      message.get_header()->message_type() != MessageType::ReductionOver) {
    throw Exception{
        "The message must be a Reduction or ReductionOver but got " +
        findus::detail::get_output(message.get_header()->message_type())};
  }
  const auto reduction_id = get_id(message);
  std::uint64_t index = reduction_id;
  for (std::uint64_t counter = 0; counter < inter_process_entries_.size();
       (void)++index, (void)++counter) {  // loop for linear probing
    index = index bitand (inter_process_entries_.size() - 1);
    const std::uint64_t probed_reduction_id =
        inter_process_entries_[index].reduction_id;
    if (probed_reduction_id == reduction_id) {
      // We are combining with an existing message. Since we aren't guaranteed
      // that the local message contributed first, we handle the total
      // number of contributions by always adding the total contributions from
      // the incoming message, which is zero except when it comes locally.
      set_expected_number_of_contributions(
          inter_process_entries_[index].message,
          get_expected_number_of_contributions(
              inter_process_entries_[index].message) +
              get_expected_number_of_contributions(message) - 1);
      if (message.get_header()->source_process_id() ==
          p_and_c.self_process_id) {
        set_target_process_id(inter_process_entries_[index].message,
                              get_target_process_id(message));
        set_expected_number_of_root_contributions(
            inter_process_entries_[index].message,
            get_expected_number_of_root_contributions(message));
      }
      get_combine_function_pointer(message)(
          inter_process_entries_[index].message, message);
      break;
    } else {
      // We insert the message
      if (probed_reduction_id != 0) {
        // The entry is used by another key.
        continue;
      }
      inter_process_entries_[index].reduction_id = reduction_id;
      inter_process_entries_[index].message = std::move(message);
      if (inter_process_entries_[index]
                  .message.get_header()
                  ->destination_process_id() == 0 and
          get_expected_number_of_root_contributions(
              inter_process_entries_[index].message) != 0) {
        // If we are the root node and the message tells us how many
        // contributions we should expect, then we need to set the number of
        // expected contributions from that.
        set_expected_number_of_contributions(
            inter_process_entries_[index].message,
            get_expected_number_of_root_contributions(
                inter_process_entries_[index].message) -
                1);
      } else {
        // Otherwise we set the number of contributions we are still waiting
        // for but decrement by 1 since the current message is contributing.
        set_expected_number_of_contributions(
            inter_process_entries_[index].message,
            get_expected_number_of_contributions(
                inter_process_entries_[index].message) -
                1);
      }
      break;
    }
  }
  if (get_expected_number_of_contributions(
          inter_process_entries_[index].message) == 0) {
    inter_process_entries_[index].reduction_id = 0;
    inter_process_entries_[index]
        .message.get_header()
        ->change_destination_process_id(
            get_target_process_id(inter_process_entries_[index].message));
    inter_process_entries_[index]
        .message.get_header()
        ->change_source_process_id(p_and_c.self_process_id);
    return {std::move(inter_process_entries_[index].message)};
  }
  return std::nullopt;
}
}  // namespace findus::reduction

#if defined(FINDUS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <random>
#include <thread>
#include <tuple>
#include <vector>

#include "findus/Detail/GetOutput.hpp"
#include "findus/DistributedObjectCollection.hpp"
#include "findus/Message.hpp"
#include "findus/Reduction.hpp"
#include "findus/Serialize/Stl/Vector.hpp"

namespace findus::reduction {
namespace {
void test_insert_action_stream_operator() {
  using findus::detail::get_output;

  CHECK(get_output(InsertAction::Insert) == "Insert");
  CHECK(get_output(InsertAction::Combine) == "Combine");
  CHECK(get_output(InsertAction::Complete) == "Complete");
  CHECK(get_output(InsertAction::AtCapacity) == "AtCapacity");
}

struct DummyAction {};
struct DummyComponent
    : public findus::DistributedObjectCollection<DummyComponent> {
  using findus_collection_index = std::int64_t;
};

template <class Action, class Component, class... Args>
struct DummyCallback {
  int value;
  DummyCallback(int v) : value(v) {}
  bool operator==(const DummyCallback& other) const {
    return value == other.value;
  }
};

void test_combine_function() {
  using namespace findus::reduction;
  using DataTuple = std::tuple<int, double>;
  using DataTupleVector = std::tuple<std::vector<int>, std::vector<double>>;

  // Simple binary op: sum for int, product for double
  struct SumProductOp {
    void operator()(DataTuple& lhs, const int rhs_int,
                    const double rhs_double) const {
      std::get<0>(lhs) += rhs_int;
      std::get<1>(lhs) *= rhs_double;
    }

    void operator()(DataTupleVector& lhs, const std::vector<int>& rhs_int,
                    const std::vector<double>& rhs_double) const {
      auto& lhs0 = std::get<0>(lhs);
      auto& lhs1 = std::get<1>(lhs);

      if (lhs0.size() != rhs_int.size()) {
        throw Exception{"Different sizes!"};
      }
      if (lhs1.size() != rhs_double.size()) {
        throw Exception{"Different sizes!"};
      }

      for (size_t i = 0; i < lhs0.size(); ++i) {
        lhs0[i] += rhs_int[i];
      }
      for (size_t i = 0; i < lhs1.size(); ++i) {
        lhs1[i] *= rhs_double[i];
      }
    }
  };

  // Prepare two messages with the same reduction id
  const std::uint32_t distributed_object_index = 1;
  const std::uint64_t reduction_id = 1234;
  {
    DataTuple data0{2, 3.0};
    DataTuple data1{5, 4.0};

    // Dummy callback (not used in combine)
    using CallbackType = ReductionCallback<DummyAction, DummyComponent>;
    CallbackType callback{0};

    // Create two messages
    Message_t msg0 =
        create_message(distributed_object_index, reduction_id, data0, callback,
                       MessageType::Reduction, false);
    Message_t msg1 =
        create_message(distributed_object_index, reduction_id, data1, callback,
                       MessageType::Reduction, false);

    // Combine msg1 into msg0
    detail::combine<SumProductOp, DataTuple>(msg0, msg1);

    // Check result: (2+5, 3.0*4.0) = (7, 12.0)
    const DataTuple* result =
        findus::data_from_message<DataTuple>(*msg0.get_header());
    CHECK(std::get<0>(*result) == 7);
    CHECK(std::get<1>(*result) == 12.0);

    // Test error: mismatched reduction id
    Message_t msg2 =
        create_message(distributed_object_index, reduction_id + 1, data1,
                       callback, MessageType::Reduction, false);
    CHECK_THROWS_WITH_AS((detail::combine<SumProductOp, DataTuple>(msg0, msg2)),
                         "The reduction id in the two reduction messages must "
                         "match but message0 has: 1234 and message1 has: 1235",
                         findus::Exception);
  }

  {
    DataTupleVector data0{std::vector<int>{2, 3, 4, 5},
                          std::vector<double>{3.0, 4.0, 5.0}};
    DataTupleVector data1{std::vector<int>{5, 6, 7, 8},
                          std::vector<double>{4.0, 6.0, 8.0}};

    // Dummy callback (not used in combine)
    using CallbackType = ReductionCallback<DummyAction, DummyComponent>;
    CallbackType callback{0};

    Message_t msg0 =
        create_message(distributed_object_index, reduction_id, data0, callback,
                       MessageType::Reduction, true);
    CHECK(msg0.get_header()->data_was_serialized());
    Message_t msg1 =
        create_message(distributed_object_index, reduction_id, data1, callback,
                       MessageType::Reduction, true);
    CHECK(msg1.get_header()->data_was_serialized());

    detail::combine<SumProductOp, DataTupleVector>(msg0, msg1);
    CHECK(msg0.get_header()->data_was_serialized());

    DataTupleVector result{};
    {
      serialize::Serializer unpacker{
          serialize::Serializer::Unpacking,
          findus::reduction::get_data_pointer<std::byte>(msg0),
          findus::reduction::get_data_size(msg0)};
      unpacker | result;
    }

    CHECK(std::get<0>(result) == std::vector<int>{7, 9, 11, 13});
    CHECK(std::get<1>(result) == std::vector<double>{12.0, 24.0, 40.0});
  }
}

/// [findus_reduction_sump_op_functor]
struct SumOp {
  void operator()(std::tuple<int, double>& lhs, const int rhs_int,
                  const double rhs_double) const {
    std::get<0>(lhs) += rhs_int;
    std::get<1>(lhs) += rhs_double;
  }
};
/// [findus_reduction_sump_op_functor]

void test_data_handler_parallel(const size_t num_reductions,
                                const size_t max_entries = 0) {
  INFO("Test DataHandler with " << num_reductions << " reductions");

  using DataTuple = std::tuple<int, double>;
  using CallbackType = ReductionCallback<DummyAction, DummyComponent>;

  const size_t rng_seed = 4444;
  const std::uint32_t max_per_thread_contributions = 100;
  const size_t entries =
      max_entries == 0 ? std::max(num_reductions * 2ul, 4ul) : max_entries;
  const std::uint32_t distributed_object_index = 42;
  DataHandler handler(entries);

  CHECK(handler.capacity() == entries);

  // Randomize contributions per thread for each reduction
  std::vector<int> number_of_contributions(num_reductions, 1);
  {
    std::mt19937 rng{rng_seed};
    for (size_t r = 0; r < num_reductions; ++r) {
      number_of_contributions[r] = 1 + (rng() % max_per_thread_contributions);
    }
  }

  // Each thread inserts or combines for all reductions
  int count_inserts{0};
  int count_combines{0};
  for (size_t r = 0; r < num_reductions; ++r) {
    for (int i = 0; i < number_of_contributions[r]; ++i) {
      const int int_value = 1000 * int(r) + i;
      const double double_value = 0.5 * int_value;
      const InsertAction result = handler.insert_or_combine<SumOp>(
          r % 2 == 0 ? MessageType::Reduction : MessageType::ReductionOver,
          distributed_object_index, r + 1, CallbackType{}, int_value,
          double_value);
      // First insert should be Insert, others Combine
      if (result == InsertAction::Insert) {
        count_inserts++;
      } else {
        count_combines++;
      }
    }
  }
  CHECK(count_inserts == num_reductions);
  CHECK(count_combines ==
        (std::accumulate(std::begin(number_of_contributions),
                         std::end(number_of_contributions), 0) -
         count_inserts));

  // Now, for each reduction, pop and check the result
  for (size_t r = 0; r < num_reductions; ++r) {
    CAPTURE(r + 1);
    // index_of should be valid before pop
    auto idx = handler.index_of(r + 1);
    CHECK(idx.has_value());

    Message_t msg = handler.pop(r + 1);

    CHECK(msg.get_header()->message_type() ==
          (r % 2 == 0 ? MessageType::Reduction : MessageType::ReductionOver));

    // index_of should be invalid after pop
    CHECK_FALSE(handler.index_of(r + 1).has_value());

    // Check the data
    const auto* data = data_from_message<DataTuple>(*msg.get_header());
    int expected_sum = 0;
    double expected_dsum = 0.0;
    for (int i = 0; i < number_of_contributions[r]; ++i) {
      int value = 1000 * int(r) + i;
      expected_sum += value;
      expected_dsum += 0.5 * value;
    }
    CHECK(std::get<0>(*data) == expected_sum);
    CHECK(std::abs(std::get<1>(*data) - expected_dsum) < 1e-8);

    // Check the callback
    const CallbackType* cb = reduction::get_callback<CallbackType>(msg);
    CHECK(cb != nullptr);
  }

  // Popping again should throw
  for (size_t r = 0; r < num_reductions; ++r) {
    const std::string msg =
        "Could not find reduction ID " + std::to_string(r + 1);
    CHECK_THROWS_WITH_AS(handler.pop(r + 1), msg.c_str(), findus::Exception);
  }
}

void test_data_handler_exceptions() {
  INFO("Test DataHandler throws for invalid reduction_id and full container");
  using CallbackType = ReductionCallback<DummyAction, DummyComponent>;

  DataHandler handler(2);

  // reduction_id == 0 should throw
  CHECK_THROWS_WITH_AS(
      handler.insert_or_combine<SumOp>(MessageType::Reduction, 42, 0,
                                       CallbackType{}, 1, 2.0),
      "The key value of 0 is not supported in reductions because it is used as "
      "a sentinel.",
      findus::Exception);
  CHECK_THROWS_WITH_AS(
      handler.insert_or_combine<SumOp>(MessageType::Invoke, 42, 1,
                                       CallbackType(1), 1, 2.0),
      "MessageType passed to DataHandler::insert_or_combine must be "
      "Reduction or ReductionOver but got Invoke",
      findus::Exception);

  // Fill all slots
  handler.insert_or_combine<SumOp>(MessageType::Reduction, 42, 1,
                                   CallbackType(1), 1, 2.0);
  handler.insert_or_combine<SumOp>(MessageType::Reduction, 42, 2,
                                   CallbackType(2), 2, 3.0);

  // Now try to insert another key, should throw
  CHECK(handler.insert_or_combine<SumOp>(MessageType::Reduction, 42, 3,
                                         CallbackType(3), 3,
                                         4.0) == InsertAction::AtCapacity);
}

void test_reduction_callback() {
  using Callback = ReductionCallback<DummyAction, DummyComponent>;

  // Test default constructor (Broadcast)
  const Callback cb_broadcast{};
  CHECK(cb_broadcast.collection_index_ == MessageHeader::no_collection_index());
  CHECK(cb_broadcast.distributed_object_index_ ==
        findus::detail::distributed_object_index<DummyComponent>());
  CHECK(cb_broadcast.message_type_ == MessageType::Broadcast);

  // Test constructor with collection index (Invoke)
  const std::int64_t index = 42;
  const Callback cb_invoke{index};
  CHECK(cb_invoke.collection_index_ == findus::detail::to_internal(index));
  CHECK(cb_invoke.distributed_object_index_ ==
        findus::detail::distributed_object_index<DummyComponent>());
  CHECK(cb_invoke.message_type_ == MessageType::Invoke);

  // Test equality and inequality
  const Callback cb_broadcast2{};
  const Callback cb_invoke2{43};
  CHECK(cb_broadcast == cb_broadcast2);
  CHECK(cb_broadcast != cb_invoke);
  CHECK(cb_invoke != cb_invoke2);
  CHECK(cb_invoke == Callback{index});
}
}  // namespace
}  // namespace findus::reduction

TEST_CASE("Reduction") {
  findus::reduction::test_insert_action_stream_operator();
  findus::reduction::test_combine_function();

  for (const auto num_reductions : {0ul, 1ul, 2ul, 8ul, 16ul, 32ul}) {
    findus::reduction::test_data_handler_parallel(num_reductions);
  }
  findus::reduction::test_data_handler_exceptions();
  findus::reduction::test_reduction_callback();
}

#endif
