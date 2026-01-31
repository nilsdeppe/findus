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
    Message_t t = std::move(entries_[index.value()].callback_and_data);
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
    CHECK_THROWS_WITH_AS((void)handler.pop(r + 1), msg.c_str(),
                         findus::Exception);
  }
}

void test_data_handler_exceptions() {
  INFO("Test DataHandler throws for invalid reduction_id and full container");
  using CallbackType = ReductionCallback<DummyAction, DummyComponent>;

  DataHandler handler(2);

  // reduction_id == 0 should throw
  CHECK_THROWS_WITH_AS(
      (void)handler.insert_or_combine<SumOp>(MessageType::Reduction, 42, 0,
                                             CallbackType{}, 1, 2.0),
      "The key value of 0 is not supported in reductions because it is used as "
      "a sentinel.",
      findus::Exception);
  CHECK_THROWS_WITH_AS(
      (void)handler.insert_or_combine<SumOp>(MessageType::Invoke, 42, 1,
                                             CallbackType(1), 1, 2.0),
      "MessageType passed to DataHandler::insert_or_combine must be "
      "Reduction or ReductionOver but got Invoke",
      findus::Exception);

  // Fill all slots
  CHECK(handler.insert_or_combine<SumOp>(MessageType::Reduction, 42, 1,
                                         CallbackType(1), 1,
                                         2.0) == InsertAction::Insert);
  CHECK(handler.insert_or_combine<SumOp>(MessageType::Reduction, 42, 2,
                                         CallbackType(2), 2,
                                         3.0) == InsertAction::Insert);

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

// Test data types for Handler tests
struct Simple : public findus::serialize::as_bytes<void> {
  int a{0};
  double b{0.0};

  Simple() = default;
  Simple(const int a_in, const double b_in) : a(a_in), b(b_in) {}

  // serialize::Serializer& serialize(serialize::Serializer& s) {
  //   return s | a | b;
  // }

  bool operator==(const Simple &) const = default;
};

struct Complex {
  int a{0};
  std::vector<double> b{};

  serialize::Serializer &serialize(serialize::Serializer &s) {
    return s | a | b;
  }

  bool operator==(const Complex &) const = default;
};

// Binary operations for Handler tests
struct SimpleSumOp {
  void operator()(std::tuple<Simple> &lhs, const Simple &rhs) const {
    std::get<0>(lhs).a += rhs.a;
    std::get<0>(lhs).b += rhs.b;
  }
};

struct ComplexSumOp {
  void operator()(std::tuple<Complex> &lhs, const Complex &rhs) const {
    std::get<0>(lhs).a += rhs.a;
    auto &lhs_b = std::get<0>(lhs).b;
    for (size_t i = 0; i < lhs_b.size(); ++i) {
      lhs_b[i] += rhs.b[i];
    }
  }
};

void test_handler_construction() {
  INFO("Test Handler construction");

  // Basic construction. No exceptions means success.
  { Handler handler(4, 16); }

  // Various thread counts
  for (const size_t num_threads : std::vector<size_t>{1, 2, 4, 8, 16}) {
    for (const size_t max_reductions : std::vector<size_t>{4, 16, 64}) {
      Handler handler(num_threads, max_reductions);
    }
  }
}

void test_handler_insert_or_combine_simple() {
  INFO("Test Handler::insert_or_combine with Simple (non-serialized)");

  using CallbackType = ReductionCallback<DummyAction, DummyComponent>;
  const std::uint32_t distributed_object_index = 1;
  const std::uint64_t reduction_id = 17;

  // Test single contribution completes immediately
  {
    Handler handler(2, 16);
    const auto compute_expected = []() { return 1; };

    std::optional<Message_t> result = handler.insert_or_combine<SimpleSumOp>(
        compute_expected, MessageType::Reduction,
        1, // thread_id
        distributed_object_index, reduction_id, CallbackType{},
        Simple{10, 2.5});

    REQUIRE(result.has_value());
    const auto *data =
        data_from_message<std::tuple<Simple>>(*result->get_header());
    REQUIRE(data != nullptr);
    CHECK(std::get<0>(*data).a == 10);
    CHECK(std::get<0>(*data).b == 2.5);
  }

  // Test multiple contributions combine correctly
  {
    Handler handler(4, 16);
    const auto compute_expected = []() { return 3; };

    // First contribution - should not complete
    std::optional<Message_t> result1 = handler.insert_or_combine<SimpleSumOp>(
        compute_expected, MessageType::Reduction,
        1, // thread_id
        distributed_object_index, reduction_id, CallbackType{},
        Simple{10, 1.0});
    CHECK_FALSE(result1.has_value());

    // Second contribution - should not complete
    std::optional<Message_t> result2 = handler.insert_or_combine<SimpleSumOp>(
        compute_expected, MessageType::Reduction,
        2, // thread_id
        distributed_object_index, reduction_id, CallbackType{},
        Simple{20, 2.0});
    CHECK_FALSE(result2.has_value());

    // Third contribution - should complete
    std::optional<Message_t> result3 = handler.insert_or_combine<SimpleSumOp>(
        compute_expected, MessageType::Reduction,
        3, // thread_id
        distributed_object_index, reduction_id, CallbackType{},
        Simple{30, 3.0});
    REQUIRE(result3.has_value());

    const auto *data =
        data_from_message<std::tuple<Simple>>(*result3->get_header());
    REQUIRE(data != nullptr);
    CHECK(std::get<0>(*data).a == 60);  // 10 + 20 + 30
    CHECK(std::get<0>(*data).b == 6.0); // 1.0 + 2.0 + 3.0
  }

  // Test callback is preserved at the level of that the pointer is not
  // null and that the index is correct.
  {
    Handler handler(2, 16);
    const auto compute_expected = []() { return 1; };
    const std::int64_t callback_index = 42;

    std::optional<Message_t> result = handler.insert_or_combine<SimpleSumOp>(
        compute_expected, MessageType::Reduction, 1, distributed_object_index,
        reduction_id, CallbackType{callback_index}, Simple{1, 1.0});

    REQUIRE(result.has_value());
    const CallbackType *callback = get_callback<CallbackType>(*result);
    REQUIRE(callback != nullptr);
    CHECK(callback->collection_index_ ==
          findus::detail::to_internal(callback_index));
  }
}

void test_handler_insert_or_combine_complex() {
  INFO("Test Handler::insert_or_combine with Complex (serialized)");

  using CallbackType = ReductionCallback<DummyAction, DummyComponent>;
  const std::uint32_t distributed_object_index = 2;
  const std::uint64_t reduction_id = 67890;

  {
    INFO("Test single contribution");
    Handler handler(2, 16);
    const auto compute_expected = []() { return 1; };

    std::optional<Message_t> result = handler.insert_or_combine<ComplexSumOp>(
        compute_expected, MessageType::Reduction, 1, distributed_object_index,
        reduction_id, CallbackType{}, Complex{5, {1.0, 2.0, 3.0}});

    REQUIRE(result.has_value());
    CHECK(result->get_header()->data_was_serialized() == false);

    const auto *data =
        data_from_message<std::tuple<Complex>>(*result->get_header());
    REQUIRE(data != nullptr);
    CHECK(std::get<0>(*data).a == 5);
    CHECK(std::get<0>(*data).b == std::vector<double>{1.0, 2.0, 3.0});
  }

  {
    INFO("Test multiple contributions combine correctly");
    Handler handler(4, 16);
    const auto compute_expected = []() { return 3; };

    std::optional<Message_t> result1 = handler.insert_or_combine<ComplexSumOp>(
        compute_expected, MessageType::Reduction, 1, distributed_object_index,
        reduction_id, CallbackType{}, Complex{1, {1.0, 1.0}});
    CHECK_FALSE(result1.has_value());

    std::optional<Message_t> result2 = handler.insert_or_combine<ComplexSumOp>(
        compute_expected, MessageType::Reduction, 2, distributed_object_index,
        reduction_id, CallbackType{}, Complex{2, {2.0, 2.0}});
    CHECK_FALSE(result2.has_value());

    std::optional<Message_t> result3 = handler.insert_or_combine<ComplexSumOp>(
        compute_expected, MessageType::Reduction, 3, distributed_object_index,
        reduction_id, CallbackType{}, Complex{3, {3.0, 3.0}});
    REQUIRE(result3.has_value());

    const auto *data =
        data_from_message<std::tuple<Complex>>(*result3->get_header());
    REQUIRE(data != nullptr);
    CHECK(std::get<0>(*data).a == 6); // 1 + 2 + 3
    CHECK(std::get<0>(*data).b == std::vector<double>{6.0, 6.0});
  }
  // TODO: should  we test that if the vectors are different sizes we could
  //       get an exception that propagates through?
}

void test_handler_insert_or_combine() {
  test_handler_insert_or_combine_simple();
  test_handler_insert_or_combine_complex();
}

void test_handler_combine_inter_process_simple() {
  INFO("Test Handler::combine_inter_process with Simple (non-serialized)");

  using CallbackType = ReductionCallback<DummyAction, DummyComponent>;
  const std::uint32_t distributed_object_index = 1;
  const std::uint64_t reduction_id = 11111;

  {
    INFO("Test single message insertion and completion");
    Handler handler(2, 16);
    const findus::detail::ParentAndChildren p_and_c{0, -1, 1, 2};

    Message_t message = create_message<DummyAction, DummyComponent>(
        distributed_object_index, reduction_id,
        std::tuple<Simple>{Simple{10, 2.5}}, CallbackType{},
        MessageType::Reduction, false);
    set_combine_function_pointer(
        message, &detail::combine<SimpleSumOp, std::tuple<Simple>>);
    set_expected_number_of_contributions(message, 1);
    message.get_header()->change_source_process_id(0);

    std::optional<Message_t> result =
        handler.combine_inter_process(std::move(message), p_and_c);

    REQUIRE(result.has_value());
    const auto *data =
        data_from_message<std::tuple<Simple>>(*result->get_header());
    REQUIRE(data != nullptr);
    CHECK(std::get<0>(*data).a == 10);
    CHECK(std::get<0>(*data).b == 2.5);
  }

  {
    INFO("Test combining multiple messages");
    Handler handler(2, 16);
    const findus::detail::ParentAndChildren p_and_c{0, -1, 1, 2};

    // First message - expect 2 contributions
    Message_t message1 = create_message<DummyAction, DummyComponent>(
        distributed_object_index, reduction_id,
        std::tuple<Simple>{Simple{10, 1.0}}, CallbackType{},
        MessageType::Reduction, false);
    set_combine_function_pointer(
        message1, &detail::combine<SimpleSumOp, std::tuple<Simple>>);
    set_expected_number_of_contributions(message1, 2);
    set_target_process_id(message1, 0);
    message1.get_header()->change_source_process_id(1);

    std::optional<Message_t> result1 =
        handler.combine_inter_process(std::move(message1), p_and_c);
    CHECK_FALSE(result1.has_value());

    // Second message
    Message_t message2 = create_message<DummyAction, DummyComponent>(
        distributed_object_index, reduction_id,
        std::tuple<Simple>{Simple{20, 2.0}}, CallbackType{},
        MessageType::Reduction, false);
    set_combine_function_pointer(
        message2, &detail::combine<SimpleSumOp, std::tuple<Simple>>);
    set_expected_number_of_contributions(message2, 0);
    message2.get_header()->change_source_process_id(2);

    std::optional<Message_t> result2 =
        handler.combine_inter_process(std::move(message2), p_and_c);
    REQUIRE(result2.has_value());

    const auto *data =
        data_from_message<std::tuple<Simple>>(*result2->get_header());
    REQUIRE(data != nullptr);
    CHECK(std::get<0>(*data).a == 30);  // 10 + 20
    CHECK(std::get<0>(*data).b == 3.0); // 1.0 + 2.0
  }
}

void test_handler_combine_inter_process_complex() {
  INFO("Test Handler::combine_inter_process with Complex (serialized)");

  using CallbackType = ReductionCallback<DummyAction, DummyComponent>;
  const std::uint32_t distributed_object_index = 2;
  const std::uint64_t reduction_id = 22222;

  const auto helper = [](const bool serialize_first_message,
                         const bool serialize_second_message) {
    CAPTURE(serialize_first_message);
    CAPTURE(serialize_second_message);
    Handler handler(2, 16);
    const findus::detail::ParentAndChildren p_and_c{0, -1, 1, 2};

    Message_t message1 = create_message<DummyAction, DummyComponent>(
        distributed_object_index, reduction_id,
        std::tuple<Complex>{Complex{1, {1.0, 2.0}}}, CallbackType{},
        MessageType::Reduction, true);
    set_combine_function_pointer(
        message1, &detail::combine<ComplexSumOp, std::tuple<Complex>>);
    set_expected_number_of_contributions(message1, 2);
    set_target_process_id(message1, 0);
    message1.get_header()->change_source_process_id(1);

    std::optional<Message_t> result1 =
        handler.combine_inter_process(std::move(message1), p_and_c);
    CHECK_FALSE(result1.has_value());

    Message_t message2 = create_message<DummyAction, DummyComponent>(
        distributed_object_index, reduction_id,
        std::tuple<Complex>{Complex{2, {3.0, 4.0}}}, CallbackType{},
        MessageType::Reduction, true);
    set_combine_function_pointer(
        message2, &detail::combine<ComplexSumOp, std::tuple<Complex>>);
    set_expected_number_of_contributions(message2, 0);
    message2.get_header()->change_source_process_id(2);

    std::optional<Message_t> result2 =
        handler.combine_inter_process(std::move(message2), p_and_c);
    REQUIRE(result2.has_value());
    CHECK(result2->get_header()->data_was_serialized());

    // Deserialize and check
    std::tuple<Complex> unpacked_data{};
    serialize::Serializer unpacker{serialize::Serializer::Unpacking,
                                   get_data_pointer<std::byte>(*result2),
                                   get_data_size(*result2)};
    unpacker | unpacked_data;

    CHECK(std::get<0>(unpacked_data).a == 3); // 1 + 2
    CHECK(std::get<0>(unpacked_data).b == std::vector<double>{4.0, 6.0});
  };
  helper(true, true);
  helper(true, false);
  helper(false, true);
  helper(false, false);
}

void test_handler_combine_inter_process_realistic_tree() {
  INFO("Test Handler::combine_inter_process with realistic tree topology");

  using CallbackType = ReductionCallback<DummyAction, DummyComponent>;
  const std::uint32_t distributed_object_index = 1;
  const std::uint64_t reduction_id = 33333;

  /*
   * Test with 4 processes: tree structure
   *       0
   *      / \
   *     1   2
   *    /
   *   3
   * Process 1 receives from 3
   * Process 0 receives from 1 and 2
   * We need separate handlers for each "process" since each process has
   * its own Handler instance in the real system
   */
  Handler handler_pid1(2, 16);
  Handler handler_pid0(2, 16);

  Message_t combined_from_1;

  // Step 1: Process 1 receives from process 3 and combines with local
  {
    const findus::detail::ParentAndChildren p_and_c =
        findus::detail::parent_and_children(1, 4);

    Message_t message_from_3 = create_message<DummyAction, DummyComponent>(
        distributed_object_index, reduction_id,
        std::tuple<Simple>{Simple{30, 3.0}}, CallbackType{},
        MessageType::Reduction, false);
    set_combine_function_pointer(
        message_from_3, &detail::combine<SimpleSumOp, std::tuple<Simple>>);
    set_expected_number_of_contributions(message_from_3, 2);
    set_target_process_id(message_from_3, 1);
    message_from_3.get_header()->change_source_process_id(3);

    const std::optional<Message_t> result1 =
        handler_pid1.combine_inter_process(std::move(message_from_3), p_and_c);
    CHECK_FALSE(result1.has_value());

    // Process 1's local contribution
    Message_t message_from_1 = create_message<DummyAction, DummyComponent>(
        distributed_object_index, reduction_id,
        std::tuple<Simple>{Simple{10, 1.0}}, CallbackType{},
        MessageType::Reduction, false);
    set_combine_function_pointer(
        message_from_1, &detail::combine<SimpleSumOp, std::tuple<Simple>>);
    set_expected_number_of_contributions(message_from_1, 0);
    set_target_process_id(message_from_1, 0);
    message_from_1.get_header()->change_source_process_id(1);

    std::optional<Message_t> result2 =
        handler_pid1.combine_inter_process(std::move(message_from_1), p_and_c);
    REQUIRE(result2.has_value());

    const auto *data =
        data_from_message<std::tuple<Simple>>(*result2->get_header());
    REQUIRE(data != nullptr);
    CHECK(std::get<0>(*data).a == 40);  // 30 + 10
    CHECK(std::get<0>(*data).b == 4.0); // 3.0 + 1.0

    combined_from_1 = std::move(*result2);
  }

  // Step 2: Process 0 receives combined message from process 1
  {
    const findus::detail::ParentAndChildren p_and_c =
        findus::detail::parent_and_children(0, 4);

    // The combined message from process 1 is sent to process 0
    // Update expected contributions: process 0 expects from 1, 2, and self
    set_expected_number_of_contributions(combined_from_1, 3);

    const std::optional<Message_t> result1 =
        handler_pid0.combine_inter_process(std::move(combined_from_1), p_and_c);
    CHECK_FALSE(result1.has_value());

    // Step 3: Process 2 sends its contribution to process 0
    Message_t message_from_2 = create_message<DummyAction, DummyComponent>(
        distributed_object_index, reduction_id,
        std::tuple<Simple>{Simple{20, 2.0}}, CallbackType{},
        MessageType::Reduction, false);
    set_combine_function_pointer(
        message_from_2, &detail::combine<SimpleSumOp, std::tuple<Simple>>);
    set_expected_number_of_contributions(message_from_2, 0);
    set_target_process_id(message_from_2, 0);
    message_from_2.get_header()->change_source_process_id(2);

    const std::optional<Message_t> result2 =
        handler_pid0.combine_inter_process(std::move(message_from_2), p_and_c);
    CHECK_FALSE(result2.has_value());

    // Step 4: Process 0's local contribution
    Message_t message_from_0 = create_message<DummyAction, DummyComponent>(
        distributed_object_index, reduction_id,
        std::tuple<Simple>{Simple{100, 10.0}}, CallbackType{},
        MessageType::Reduction, false);
    set_combine_function_pointer(
        message_from_0, &detail::combine<SimpleSumOp, std::tuple<Simple>>);
    set_expected_number_of_contributions(message_from_0, 0);
    set_target_process_id(message_from_0, 0);
    message_from_0.get_header()->change_source_process_id(0);

    const std::optional<Message_t> result3 =
        handler_pid0.combine_inter_process(std::move(message_from_0), p_and_c);
    REQUIRE(result3.has_value());

    // Final combined result:
    // 30 + 10 + 20 + 100 = 160
    // 3.0 + 1.0 + 2.0 + 10.0 = 16.0
    const auto *data =
        data_from_message<std::tuple<Simple>>(*result3->get_header());
    REQUIRE(data != nullptr);
    CHECK(std::get<0>(*data).a == 160);
    CHECK(std::get<0>(*data).b == 16.0);
  }
}

void test_handler_combine_inter_process() {
  test_handler_combine_inter_process_simple();
  test_handler_combine_inter_process_complex();
  test_handler_combine_inter_process_realistic_tree();
}

void test_handler_set_interprocess_message_info_collection_basic() {
  INFO("Test Handler::set_interprocess_message_info (collection) basic cases");

  using CallbackType = ReductionCallback<DummyAction, DummyComponent>;
  const std::uint32_t distributed_object_index = 1;
  const std::uint64_t reduction_id = 44444;

  Handler handler(2, 16);

  // Case 1: All 4 processes have elements, all participate
  {
    const int total_processes = 4;
    std::vector<std::vector<std::uint64_t>> elements_on_pid = {
        {0, 1}, {2, 3}, {4, 5}, {6, 7}};
    const auto all_participate = [](auto) { return true; };

    for (int pid = 0; pid < total_processes; ++pid) {
      if (elements_on_pid[static_cast<size_t>(pid)].empty()) {
        continue;
      }

      Message_t message = create_message<DummyAction, DummyComponent>(
          distributed_object_index, reduction_id,
          std::tuple<Simple>{Simple{1, 1.0}}, CallbackType{},
          MessageType::Reduction, false);

      handler.set_interprocess_message_info<DummyComponent>(
          message, pid, total_processes, elements_on_pid, all_participate);

      const int expected_parent =
          pid == 0 ? -1
                   : findus::detail::find_first_parent(
                         pid, total_processes, [&elements_on_pid](const int p) {
                           return not elements_on_pid[static_cast<size_t>(p)]
                                          .empty();
                         });
      const int expected_contributions =
          findus::detail::count_first_descendants(
              pid, total_processes,
              [&elements_on_pid](const int p) {
                return not elements_on_pid[static_cast<size_t>(p)].empty();
              }) +
          1;

      CHECK(get_target_process_id(message) == std::max(expected_parent, 0));
      CHECK(get_expected_number_of_contributions(message) ==
            expected_contributions);
      CHECK(message.get_header()->source_process_id() == pid);
    }
  }

  // Case 2: Root process has no elements
  {
    const int total_processes = 4;
    std::vector<std::vector<std::uint64_t>> elements_on_pid = {
        {}, {0, 1}, {2, 3}, {4, 5}};
    const auto all_participate = [](auto) { return true; };

    // Test from process 1's perspective
    Message_t message = create_message<DummyAction, DummyComponent>(
        distributed_object_index, reduction_id,
        std::tuple<Simple>{Simple{1, 1.0}}, CallbackType{},
        MessageType::Reduction, false);

    handler.set_interprocess_message_info<DummyComponent>(
        message, 1, total_processes, elements_on_pid, all_participate);

    // Process 1's parent is 0, but 0 has no elements, so target is 0
    CHECK(get_target_process_id(message) == 0);
    // Root contributions should be set since root doesn't participate
    CHECK(get_expected_number_of_root_contributions(message) > 0);
  }

  // Case 3: Only leaf processes have elements
  {
    const int total_processes = 4;
    std::vector<std::vector<std::uint64_t>> elements_on_pid = {
        {}, {}, {}, {0, 1}};
    const auto all_participate = [](auto) { return true; };

    Message_t message = create_message<DummyAction, DummyComponent>(
        distributed_object_index, reduction_id,
        std::tuple<Simple>{Simple{1, 1.0}}, CallbackType{},
        MessageType::Reduction, false);

    handler.set_interprocess_message_info<DummyComponent>(
        message, 3, total_processes, elements_on_pid, all_participate);

    CHECK(get_target_process_id(message) == 0);
    CHECK(message.get_header()->source_process_id() == 3);
  }

  // Case 4: Single process has elements
  {
    const int total_processes = 4;
    std::vector<std::vector<std::uint64_t>> elements_on_pid = {
        {}, {0, 1, 2}, {}, {}};
    const auto all_participate = [](auto) { return true; };

    Message_t message = create_message<DummyAction, DummyComponent>(
        distributed_object_index, reduction_id,
        std::tuple<Simple>{Simple{1, 1.0}}, CallbackType{},
        MessageType::Reduction, false);

    handler.set_interprocess_message_info<DummyComponent>(
        message, 1, total_processes, elements_on_pid, all_participate);

    CHECK(get_expected_number_of_contributions(message) == 1);
  }

  // Case 5: Predicate filters some elements
  {
    const int total_processes = 4;
    std::vector<std::vector<std::uint64_t>> elements_on_pid = {
        {0, 1}, {2, 3}, {4, 5}, {6, 7}};
    // Only even-indexed elements participate
    const auto even_only = [](auto idx) {
      return (findus::detail::to_internal(idx) % 2) == 0;
    };

    Message_t message = create_message<DummyAction, DummyComponent>(
        distributed_object_index, reduction_id,
        std::tuple<Simple>{Simple{1, 1.0}}, CallbackType{},
        MessageType::Reduction, false);

    handler.set_interprocess_message_info<DummyComponent>(
        message, 0, total_processes, elements_on_pid, even_only);

    CHECK(message.get_header()->source_process_id() == 0);
  }
}

void test_handler_set_interprocess_message_info_collection_exhaustive() {
  INFO("Test Handler::set_interprocess_message_info (collection) exhaustive");

  using CallbackType = ReductionCallback<DummyAction, DummyComponent>;
  const std::uint32_t distributed_object_index = 1;
  const std::uint64_t reduction_id = 55555;

  Handler handler(2, 16);

  // Test all process counts from 1 to 8
  for (int total_processes = 1; total_processes <= 8; ++total_processes) {
    // Generate all combinations of 0-3 IDs per process
    // Total combinations: 4^total_processes
    const size_t num_combinations =
        static_cast<size_t>(1) << (2 * static_cast<size_t>(total_processes));

    for (size_t combo = 0; combo < num_combinations; ++combo) {
      std::vector<std::vector<std::uint64_t>> elements_on_pid(
          static_cast<size_t>(total_processes));

      std::uint64_t next_id = 0;
      size_t total_elements = 0;

      // Decode combination: 2 bits per process (0-3 elements)
      for (int pid = 0; pid < total_processes; ++pid) {
        const size_t num_elements =
            (combo >> (2 * static_cast<size_t>(pid))) & 0b11;
        for (size_t i = 0; i < num_elements; ++i) {
          elements_on_pid[static_cast<size_t>(pid)].push_back(next_id++);
        }
        total_elements += num_elements;
      }

      // Skip if no elements at all
      if (total_elements == 0) {
        continue;
      }

      // All elements participate predicate
      const auto all_participate = [](auto) { return true; };

      // Predicate to check if a process has elements that participate
      const auto pid_has_elements = [&elements_on_pid,
                                     &all_participate](const int p) {
        const auto &ids = elements_on_pid[static_cast<size_t>(p)];
        return std::any_of(
            ids.begin(), ids.end(), [&all_participate](std::uint64_t id) {
              return all_participate(
                  findus::detail::from_internal<DummyComponent>(id));
            });
      };

      // Test from each process's perspective (that has elements)
      for (int pid = 0; pid < total_processes; ++pid) {
        if (elements_on_pid[static_cast<size_t>(pid)].empty()) {
          continue;
        }

        Message_t message = create_message<DummyAction, DummyComponent>(
            distributed_object_index, reduction_id,
            std::tuple<Simple>{Simple{1, 1.0}}, CallbackType{},
            MessageType::Reduction, false);

        handler.set_interprocess_message_info<DummyComponent>(
            message, pid, total_processes, elements_on_pid, all_participate);

        // Compute expected values
        const int expected_parent =
            pid == 0 ? -1
                     : findus::detail::find_first_parent(pid, total_processes,
                                                         pid_has_elements);
        const int expected_contributions =
            findus::detail::count_first_descendants(pid, total_processes,
                                                    pid_has_elements) +
            1;

        CAPTURE(total_processes);
        CAPTURE(combo);
        CAPTURE(pid);

        CHECK(get_target_process_id(message) == std::max(expected_parent, 0));
        CHECK(get_expected_number_of_contributions(message) ==
              expected_contributions);
        CHECK(message.get_header()->source_process_id() == pid);

        // Check root contributions if applicable
        if (pid != 0 and ((expected_parent == 0 or expected_parent == -1) and
                          not pid_has_elements(0))) {
          const int expected_root_contributions =
              findus::detail::count_first_descendants(0, total_processes,
                                                      pid_has_elements);
          CHECK(get_expected_number_of_root_contributions(message) ==
                expected_root_contributions);
        }
      }
    }
  }
}

void test_handler_set_interprocess_message_info_per_process_basic() {
  INFO("Test Handler::set_interprocess_message_info (per-process) basic cases");

  using CallbackType = ReductionCallback<DummyAction, DummyComponent>;
  const std::uint32_t distributed_object_index = 1;
  const std::uint64_t reduction_id = 66666;

  Handler handler(2, 16);

  // Case 1: All processes participate
  {
    const int total_processes = 4;
    const auto all_participate = [](int) { return true; };

    for (int pid = 0; pid < total_processes; ++pid) {
      Message_t message = create_message<DummyAction, DummyComponent>(
          distributed_object_index, reduction_id,
          std::tuple<Simple>{Simple{1, 1.0}}, CallbackType{},
          MessageType::Reduction, false);

      handler.set_interprocess_message_info<DummyComponent>(
          message, pid, total_processes, all_participate);

      const int expected_parent =
          pid == 0 ? -1
                   : findus::detail::find_first_parent(pid, total_processes,
                                                       all_participate);
      const int expected_contributions =
          findus::detail::count_first_descendants(pid, total_processes,
                                                  all_participate) +
          1;

      CHECK(get_target_process_id(message) == std::max(expected_parent, 0));
      CHECK(get_expected_number_of_contributions(message) ==
            expected_contributions);
      CHECK(message.get_header()->source_process_id() == pid);
    }
  }

  // Case 2: Only odd processes participate
  {
    const int total_processes = 8;
    const auto odd_only = [](int p) { return p % 2 == 1; };

    for (int pid = 0; pid < total_processes; ++pid) {
      if (not odd_only(pid)) {
        continue;
      }

      Message_t message = create_message<DummyAction, DummyComponent>(
          distributed_object_index, reduction_id,
          std::tuple<Simple>{Simple{1, 1.0}}, CallbackType{},
          MessageType::Reduction, false);

      handler.set_interprocess_message_info<DummyComponent>(
          message, pid, total_processes, odd_only);

      const int expected_parent =
          findus::detail::find_first_parent(pid, total_processes, odd_only);
      const int expected_contributions =
          findus::detail::count_first_descendants(pid, total_processes,
                                                  odd_only) +
          1;

      CHECK(get_target_process_id(message) == std::max(expected_parent, 0));
      CHECK(get_expected_number_of_contributions(message) ==
            expected_contributions);
    }
  }

  // Case 3: Single process participates
  {
    const int total_processes = 4;
    const auto only_one = [](const int p) { return p == 2; };

    Message_t message = create_message<DummyAction, DummyComponent>(
        distributed_object_index, reduction_id,
        std::tuple<Simple>{Simple{1, 1.0}}, CallbackType{},
        MessageType::Reduction, false);

    handler.set_interprocess_message_info<DummyComponent>(
        message, 2, total_processes, only_one);

    CHECK(get_expected_number_of_contributions(message) == 1);
  }
}

void test_handler_set_interprocess_message_info_per_process_exhaustive() {
  INFO("Test Handler::set_interprocess_message_info (per-process) exhaustive");

  using CallbackType = ReductionCallback<DummyAction, DummyComponent>;
  const std::uint32_t distributed_object_index = 1;
  const std::uint64_t reduction_id = 77777;

  Handler handler(2, 16);

  // Test all process counts from 1 to 8
  for (int total_processes = 1; total_processes <= 8; ++total_processes) {
    // Generate all combinations of process participation
    // Total combinations: 2^total_processes
    const size_t num_combinations = static_cast<size_t>(1)
                                    << static_cast<size_t>(total_processes);

    for (size_t combo = 1; combo < num_combinations; ++combo) {
      // combo == 0 means no processes participate, skip
      const auto participates = [combo](const int p) {
        return (combo >> static_cast<size_t>(p)) & 1;
      };

      // Test from each participating process's perspective
      for (int pid = 0; pid < total_processes; ++pid) {
        if (not participates(pid)) {
          continue;
        }

        Message_t message = create_message<DummyAction, DummyComponent>(
            distributed_object_index, reduction_id,
            std::tuple<Simple>{Simple{1, 1.0}}, CallbackType{},
            MessageType::Reduction, false);

        handler.set_interprocess_message_info<DummyComponent>(
            message, pid, total_processes, participates);

        // Compute expected values
        const int expected_parent =
            pid == 0 ? -1
                     : findus::detail::find_first_parent(pid, total_processes,
                                                         participates);
        const int expected_contributions =
            findus::detail::count_first_descendants(pid, total_processes,
                                                    participates) +
            1;

        CAPTURE(total_processes);
        CAPTURE(combo);
        CAPTURE(pid);

        CHECK(get_target_process_id(message) == std::max(expected_parent, 0));
        CHECK(get_expected_number_of_contributions(message) ==
              expected_contributions);
        CHECK(message.get_header()->source_process_id() == pid);

        // Check root contributions if applicable
        if (pid != 0 and ((expected_parent == 0 or expected_parent == -1) and
                          not participates(0))) {
          const int expected_root_contributions =
              findus::detail::count_first_descendants(0, total_processes,
                                                      participates);
          CHECK(get_expected_number_of_root_contributions(message) ==
                expected_root_contributions);
        }
      }
    }
  }
}

void test_handler() {
  test_handler_construction();
  test_handler_insert_or_combine();
  test_handler_combine_inter_process();
  test_handler_set_interprocess_message_info_collection_basic();
  test_handler_set_interprocess_message_info_collection_exhaustive();
  test_handler_set_interprocess_message_info_per_process_basic();
  test_handler_set_interprocess_message_info_per_process_exhaustive();
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
  findus::reduction::test_handler();
}

#endif
