// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/Reduction.hpp"

#include <cstddef>
#include <cstdint>

namespace rts::reduction {
static constexpr std::ptrdiff_t data_offset_jump_in_bytes = 8;
static constexpr std::ptrdiff_t callback_offset_jump_in_bytes =
    data_offset_jump_in_bytes + 4;

void set_id(Message_t& message, const std::uint64_t reduction_id) {
  *reinterpret_cast<std::uint64_t*>(message.get_header()->data_location()) =
      reduction_id;
}

std::uint64_t get_id(const Message_t& message) {
  return *reinterpret_cast<const std::uint64_t*>(
      message.get_header()->data_location());
}

void set_data_offset(Message_t& message, const std::uint32_t data_offset) {
  *reinterpret_cast<std::uint32_t*>(std::next(
      message.get_header()->data_location(), data_offset_jump_in_bytes)) =
      data_offset;
}

std::uint32_t get_data_offset(const Message_t& message) {
  return *reinterpret_cast<const std::uint32_t*>(std::next(
      message.get_header()->data_location(), data_offset_jump_in_bytes));
}

void set_callback_offset(Message_t& message,
                         const std::uint32_t callback_offset) {
  *reinterpret_cast<std::uint32_t*>(std::next(
      message.get_header()->data_location(), callback_offset_jump_in_bytes)) =
      callback_offset;
}

std::uint32_t get_callback_offset(const Message_t& message) {
  return *reinterpret_cast<const std::uint32_t*>(std::next(
      message.get_header()->data_location(), callback_offset_jump_in_bytes));
}

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
void test_set_and_get_id() {
  INFO("Test set_id and get_id");
  // Use a base-10 integer for the dummy reduction ID
  const std::uint64_t dummy_id = 1234567890123456789;
  Message_t message =
      create_message(rts::detail::MemberFunctionPtr{}, 0, 0, 0, 0, 0, false,
                     MessageType::Reduction, std::tuple<>{});
  set_id(message, dummy_id);
  CHECK(get_id(message) == dummy_id);
}

void test_set_and_get_data_offset() {
  INFO("Test set_data_offset and get_data_offset");
  const std::uint32_t dummy_offset = 305419896;  // Example: 123456789
  Message_t message =
      create_message(rts::detail::MemberFunctionPtr{}, 0, 0, 0, 0, 0, false,
                     MessageType::Reduction, std::tuple<>{});
  set_data_offset(message, dummy_offset);
  CHECK(get_data_offset(message) == dummy_offset);
}

void test_set_and_get_callback_offset() {
  INFO("Test set_callback_offset and get_callback_offset");
  const std::uint32_t dummy_offset = 987654321;
  Message_t message =
      create_message(rts::detail::MemberFunctionPtr{}, 0, 0, 0, 0, 0, false,
                     MessageType::Reduction, std::tuple<>{});
  set_callback_offset(message, dummy_offset);
  CHECK(get_callback_offset(message) == dummy_offset);
}

void test_insert_action_stream_operator() {
  using rts::detail::get_output;

  CHECK(get_output(InsertAction::Insert) == "Insert");
  CHECK(get_output(InsertAction::Combine) == "Combine");
  CHECK(get_output(InsertAction::Complete) == "Complete");
}

}  // namespace
}  // namespace rts::reduction

TEST_CASE("ReductionSetGet") {
  rts::reduction::test_set_and_get_id();
  rts::reduction::test_set_and_get_data_offset();
  rts::reduction::test_set_and_get_callback_offset();
}

TEST_CASE("InsertAction") {
  rts::reduction::test_insert_action_stream_operator();
}

// #endif
