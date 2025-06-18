// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/MessageHeader.hpp"

#include <type_traits>

#include "Rts/Detail/MemberFunctionPtr.hpp"

namespace rts {
std::ostream& operator<<(std::ostream& os, MessageType t) {
  switch (t) {
    case MessageType::Uninitialized:
      return os << "Uninitialized";
    case MessageType::Invoke:
      return os << "Invoke";
    case MessageType::Broadcast:
      return os << "Broadcast";
    case MessageType::BroadcastTo:
      return os << "BroadcastTo";
    case MessageType::Reduction:
      return os << "Reduction";
    case MessageType::SubsetReduction:
      return os << "SubsetReduction";
    default:
      return os << "Unknown";
  }
}

namespace {
void set_data_was_serialized(std::uint64_t& metadata,
                             const bool data_was_serialized) {
  if (data_was_serialized) {
    // Set highest bit to 1
    metadata = std::uint64_t{0b1} << 63 bitor metadata;
  } else {
    // Set highest bit to zero
    metadata =
        std::uint64_t{std::numeric_limits<std::uint64_t>::max() >> 1} bitand
        metadata;
  }
}

void set_message_type(std::uint64_t& message_metadata,
                      const MessageType message_type) {
  message_metadata =
      (static_cast<std::uint64_t>(message_type) << 60) bitor message_metadata;
}
}  // namespace

MessageHeader::MessageHeader(detail::MemberFunctionPtr member_function_ptr,
                             std::uint64_t target_collection_index,
                             std::uint64_t number_of_bytes_in_message,
                             std::uint32_t distributed_object_index,
                             std::uint32_t data_offset,
                             std::int32_t source_process_id,
                             std::int32_t destination_process_id,
                             std::uint64_t quiescence_detection_sweep_number,
                             bool was_serialized, MessageType message_type)
    : member_function_ptr_(member_function_ptr),
      target_collection_index_(target_collection_index),
      number_of_bytes_in_message_(number_of_bytes_in_message),
      distributed_object_index_(distributed_object_index),
      data_offset_(data_offset),
      source_process_id_(source_process_id),
      destination_process_id_(destination_process_id),
      quiescence_detection_sweep_number_(quiescence_detection_sweep_number) {
  set_data_was_serialized(number_of_bytes_in_message_, was_serialized);
  set_message_type(number_of_bytes_in_message_, message_type);
}

static_assert(std::alignment_of_v<MessageHeader> == 64);
static_assert(
    std::is_same_v<std::underlying_type_t<MessageType>, std::uint8_t>);
}  // namespace rts

#if defined(RTS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <memory>

#include "Rts/Detail/GetOutput.hpp"

namespace rts {
namespace {
struct TestClass {
  void foo() {}
  void bar() {}
};
}  // namespace

TEST_CASE("MessageType") {
  CHECK("Uninitialized" == detail::get_output(MessageType::Uninitialized));
  CHECK("Invoke" == detail::get_output(MessageType::Invoke));
  CHECK("Broadcast" == detail::get_output(MessageType::Broadcast));
  CHECK("BroadcastTo" == detail::get_output(MessageType::BroadcastTo));
  CHECK("Reduction" == detail::get_output(MessageType::Reduction));
  CHECK("SubsetReduction" == detail::get_output(MessageType::SubsetReduction));
  CHECK("Unknown" == detail::get_output(static_cast<MessageType>(0b111)));
}

TEST_CASE("MessageHeader") {
  const auto foo_ptr = detail::to_member_function_ptr(&TestClass::foo);

  const auto test_impl = [&foo_ptr](
                             MessageHeader& message_header,
                             const MessageType expected_message_type,
                             const std::uint64_t expected_collection_index,
                             const bool expected_data_was_serialized) {
    CHECK(message_header.member_function_ptr() == foo_ptr);
    CHECK(message_header.target_collection_index() ==
          expected_collection_index);
    CHECK(message_header.number_of_bytes_in_message() == 256);
    CHECK(message_header.distributed_object_index() == 11);
    CHECK(message_header.distributed_object_index() == 11);
    CHECK(message_header.data_location() ==
          std::next(reinterpret_cast<char*>(&message_header), 128));
    CHECK(message_header.source_process_id() == 2);
    CHECK(message_header.destination_process_id() == 7);
    CHECK(message_header.quiescence_detection_sweep_number() == 8);
    CHECK(message_header.message_type() == expected_message_type);
    CHECK(message_header.data_was_serialized() == expected_data_was_serialized);
    CHECK(message_header.is_broadcast() ==
          (expected_message_type == MessageType::Broadcast));
    CHECK(message_header.is_broadcast_to() ==
          (expected_message_type == MessageType::BroadcastTo));

    CHECK(reinterpret_cast<char*>(data_from_message<int>(message_header)) ==
          std::next(reinterpret_cast<char*>(&message_header), 128));
    CHECK(*data_from_message<int>(message_header) == 13);
  };

  for (const auto message_type : {MessageType::Invoke, MessageType::Broadcast,
                                  MessageType::BroadcastTo}) {
    for (const bool data_is_serialized : {true, false}) {
      for (const std::uint64_t collection_index :
           {static_cast<std::uint64_t>(5),
            MessageHeader::no_collection_index()}) {
        constexpr size_t bytes_in_message = 256;
        std::unique_ptr<char[]> buffer{new (
            std::align_val_t(alignof(MessageHeader))) char[bytes_in_message]};
        constexpr std::uint64_t expected_data_offset = 128;
        REQUIRE(sizeof(MessageHeader) <= expected_data_offset);
        MessageHeader* message_header =
            new (buffer.get()) MessageHeader{foo_ptr,
                                             collection_index,
                                             bytes_in_message,
                                             11,
                                             expected_data_offset,
                                             2,
                                             7,
                                             8,
                                             data_is_serialized,
                                             message_type};
        int* message_data = rts::create_data_in_message<int>(*message_header);
        *message_data = 13;
        test_impl(*message_header, message_type, collection_index,
                  data_is_serialized);
      }
    }
  }
}
}  // namespace rts
#endif
