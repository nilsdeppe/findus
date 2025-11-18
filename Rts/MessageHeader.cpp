// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/MessageHeader.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

#include "Rts/Detail/GetOutput.hpp"
#include "Rts/Detail/MemberFunctionPtr.hpp"
#include "Rts/Exceptions/Exception.hpp"

namespace rts {
namespace {
void set_data_was_serialized(std::uint64_t& metadata,
                             const bool data_was_serialized) {
  if (data_was_serialized) {
    // Set highest bit to 1
    metadata = MessageHeader::data_was_serialized_mask bitor metadata;
  } else {
    // Set highest bit to zero
    metadata = (compl MessageHeader::data_was_serialized_mask) bitand metadata;
  }
}

void set_message_type(std::uint64_t& message_metadata,
                      const MessageType message_type) {
  // Zero out bits.
  message_metadata =
      (compl MessageHeader::message_type_mask) bitand message_metadata;
  // Set bits
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
  // Make sure the assumptions we make about MessageHeader are true, even if
  // tests are compiled.
  static_assert(std::is_standard_layout_v<MessageHeader>);
  static_assert(std::is_trivially_copyable_v<MessageHeader>);
  static_assert(sizeof(MessageHeader) == 64);

  // 1099511627775 is 2^40-1, the largest number we can represent with 40 bits.
  if (number_of_bytes_in_message > 1099511627775) {
    throw Exception{"Message size must be under 1099511627776 bytes but got " +
                    std::to_string(number_of_bytes_in_message)};
  }
  set_data_was_serialized(number_of_bytes_in_message_, was_serialized);
  set_message_type(number_of_bytes_in_message_, message_type);
}

void MessageHeader::data_was_serialized(const bool was_serialized) {
  set_data_was_serialized(number_of_bytes_in_message_, was_serialized);
}

void MessageHeader::change_destination_process_id(
    const std::int32_t destination_process_id) {
  destination_process_id_ = destination_process_id;
}

void MessageHeader::change_source_process_id(
    const std::int32_t source_process_id) {
  source_process_id_ = source_process_id;
}

void MessageHeader::convert_broadcast_to_invoke(
    const std::uint64_t target_collection_index) {
  if (not is_broadcast() and not is_broadcast_to()) {
    throw Exception{"Cannot convert message type " +
                    detail::get_output(message_type()) +
                    " to an Invoke message because we can only convert "
                    "Broadcast and BroadcastTo messages."};
  }
  set_message_type(number_of_bytes_in_message_, MessageType::Invoke);
  target_collection_index_ = target_collection_index;
}

bool operator==(const MessageHeader& lhs, const MessageHeader& rhs) {
  return lhs.member_function_ptr() == rhs.member_function_ptr() and
         lhs.target_collection_index() == rhs.target_collection_index() and
         lhs.number_of_bytes_in_message() ==
             rhs.number_of_bytes_in_message() and
         lhs.distributed_object_index() == rhs.distributed_object_index() and
         lhs.data_offset() == rhs.data_offset() and
         lhs.source_process_id() == rhs.source_process_id() and
         lhs.destination_process_id() == rhs.destination_process_id() and
         lhs.quiescence_detection_sweep_number() ==
             rhs.quiescence_detection_sweep_number() and
         lhs.data_alignment() == rhs.data_alignment() and
         lhs.data_was_serialized() == rhs.data_was_serialized() and
         lhs.message_type() == rhs.message_type();
}

bool operator!=(const MessageHeader& lhs, const MessageHeader& rhs) {
  return not(lhs == rhs);
}

std::ostream& operator<<(std::ostream& os, const MessageHeader& header) {
  os << "MessageHeader {"
     << "\n  member_function_ptr: " << header.member_function_ptr()
     << "\n  target_collection_index: " << header.target_collection_index()
     << "\n  number_of_bytes_in_message: "
     << header.number_of_bytes_in_message()
     << "\n  distributed_object_index: " << header.distributed_object_index()
     << "\n  data_offset: " << header.data_offset()
     << "\n  source_process_id: " << header.source_process_id()
     << "\n  destination_process_id: " << header.destination_process_id()
     << "\n  quiescence_detection_sweep_number: "
     << header.quiescence_detection_sweep_number()
     << "\n  data_alignment: " << header.data_alignment()
     << "\n  data_was_serialized: "
     << (header.data_was_serialized() ? "true" : "false")
     << "\n  message_type: " << header.message_type() << "\n}";
  return os;
}

static_assert(std::alignment_of_v<MessageHeader> == 64);
static_assert(
    std::is_same_v<std::underlying_type_t<MessageType>, std::uint8_t>);
}  // namespace rts

#if defined(RTS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <memory>

namespace rts {
namespace {
struct alignas(64) TestClass {
  void foo() {}
  void bar() {}
  std::uint64_t value;
};
bool operator==(const TestClass& lhs, const TestClass& rhs) {
  return lhs.value == rhs.value;
}
}  // namespace

TEST_CASE("MessageHeader") {
  static_assert(std::is_standard_layout_v<MessageHeader>);
  static_assert(std::is_trivially_copyable_v<MessageHeader>);
  static_assert(sizeof(MessageHeader) == 64);
  static_assert(alignof(TestClass) == 64);
  const auto foo_ptr = detail::to_member_function_ptr(&TestClass::foo);
  const auto bar_ptr = detail::to_member_function_ptr(&TestClass::bar);

  const auto test_impl = [&foo_ptr](
                             MessageHeader& message_header,
                             const MessageType expected_message_type,
                             const std::uint64_t expected_collection_index,
                             const bool expected_data_was_serialized,
                             const auto& expected_data) {
    using T = std::decay_t<decltype(expected_data)>;
    T* message_data = rts::create_data_in_message<T>(message_header);
    *message_data = expected_data;
    CHECK(message_header.member_function_ptr() == foo_ptr);
    CHECK(message_header.target_collection_index() ==
          expected_collection_index);
    CHECK(message_header.number_of_bytes_in_message() == 256);
    CHECK(message_header.distributed_object_index() == 11);
    CHECK(message_header.distributed_object_index() == 11);
    CHECK(message_header.data_location() ==
          std::next(reinterpret_cast<char*>(&message_header), 128));
    CHECK(std::as_const(message_header).data_location() ==
          std::next(reinterpret_cast<const char*>(&message_header), 128));
    CHECK(message_header.source_process_id() == 2);
    CHECK(message_header.destination_process_id() == 7);
    CHECK(message_header.quiescence_detection_sweep_number() == 8);
    CHECK(message_header.message_type() == expected_message_type);
    CHECK(message_header.data_was_serialized() == expected_data_was_serialized);
    CHECK(message_header.is_broadcast() ==
          (expected_message_type == MessageType::Broadcast));
    CHECK(message_header.is_broadcast_to() ==
          (expected_message_type == MessageType::BroadcastTo));
    CHECK(message_header.data_alignment() == alignof(T));
    message_header.set_data_alignment(0);
    CHECK(message_header.data_alignment() == 0);
    message_header.set_data_alignment(alignof(T));
    CHECK(message_header.data_alignment() == alignof(T));

    CHECK(reinterpret_cast<char*>(data_from_message<T>(message_header)) ==
          std::next(reinterpret_cast<char*>(&message_header), 128));
    CHECK(*data_from_message<T>(message_header) == expected_data);
    CHECK(*data_from_message<T>(std::as_const(message_header)) ==
          expected_data);
    CHECK(alignof(T) == message_header.data_alignment());

    const std::string expected_message =
        std::string{"The data alignment in the message ("} +
        std::to_string(alignof(T)) +
        std::string{") does not match the alignment of the returned type 1"};
    CHECK_THROWS_WITH_AS(data_from_message<char>(message_header),
                         expected_message.c_str(), Exception);
  };

  for (const auto message_type : {MessageType::Invoke, MessageType::Broadcast,
                                  MessageType::BroadcastTo}) {
    for (const bool data_is_serialized : {true, false}) {
      for (const std::uint64_t collection_index :
           {static_cast<std::uint64_t>(5),
            MessageHeader::no_collection_index()}) {
        constexpr size_t bytes_in_message = 256;
        const auto deleter = [](std::byte* ptr) {
          ::operator delete[](
              ptr, std::align_val_t(
                       std::max(alignof(MessageHeader), alignof(TestClass))));
        };
        std::unique_ptr<std::byte[], decltype(deleter)> buffer{
            new (std::align_val_t(
                std::max(alignof(MessageHeader), alignof(TestClass))))
                std::byte[bytes_in_message],
            deleter};
        constexpr std::uint64_t expected_data_offset = 128;
        REQUIRE(sizeof(MessageHeader) <= expected_data_offset);
        MessageHeader* message_header_int =
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
        const int expected_int = 13;
        test_impl(*message_header_int, message_type, collection_index,
                  data_is_serialized, expected_int);

        // Test that we can correctly handle data received over a
        // network. What that means is in order to not technically hit UB, we
        // must create the object, copy the data over, then placement-new in
        // the buffer, and copy back. This is technically not needed since we
        // are guaranteeing on send and receive that the object size is 64
        // bytes, requires no padding, and if it did, we account for that by
        // memcpy of the object into the byte stream.
        MessageHeader temp{};
        std::memcpy(&temp, buffer.get(), sizeof(MessageHeader));
        MessageHeader* message_header_int2 = new (buffer.get()) MessageHeader;
        std::memcpy(message_header_int2, &temp, sizeof(MessageHeader));
        test_impl(*message_header_int2, message_type, collection_index,
                  data_is_serialized, expected_int);

        message_header_int->member_function_ptr(bar_ptr);
        CHECK(message_header_int->member_function_ptr() != foo_ptr);
        CHECK(message_header_int->member_function_ptr() == bar_ptr);
        message_header_int->member_function_ptr(foo_ptr);
        CHECK(message_header_int->member_function_ptr() == foo_ptr);
        CHECK(message_header_int->member_function_ptr() != bar_ptr);

        message_header_int->quiescence_detection_sweep_number(111);
        CHECK(message_header_int->quiescence_detection_sweep_number() == 111);
        message_header_int->quiescence_detection_sweep_number(8);

        CHECK(message_header_int->source_process_id() == 2);
        message_header_int->change_source_process_id(111);
        CHECK(message_header_int->source_process_id() == 111);
        message_header_int->change_source_process_id(2);
        CHECK(message_header_int->source_process_id() == 2);

        CHECK(message_header_int->data_was_serialized() == data_is_serialized);
        message_header_int->data_was_serialized(not data_is_serialized);
        CHECK(message_header_int->data_was_serialized() ==
              not data_is_serialized);
        message_header_int->data_was_serialized(data_is_serialized);

        MessageHeader* message_header_test_class =
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
        const TestClass expected_test_class{19};
        test_impl(*message_header_test_class, message_type, collection_index,
                  data_is_serialized, expected_test_class);
      }
    }
  }
  CHECK_THROWS_WITH_AS(
      MessageHeader(foo_ptr, 0, 1099511627776, 11, 10, 2, 7, 8, false,
                    MessageType::Invoke),
      "Message size must be under 1099511627776 bytes but got 1099511627776",
      Exception);
  CHECK_NOTHROW(MessageHeader{foo_ptr, 0, 1099511627775, 11, 10, 2, 7, 8, false,
                              MessageType::Invoke});
  // Check alignment bits math works out as expected.
  CHECK(MessageHeader::max_alignment == 255);

  {
    INFO("Test change_destination_process_id()");
    MessageHeader header{foo_ptr, 2, 64, 0,     8,
                         3,       1, 0,  false, MessageType::Broadcast};
    CHECK(header.destination_process_id() == 1);
    header.change_destination_process_id(42);
    CHECK(header.destination_process_id() == 42);
  }

  {
    INFO("Test convert_broadcast_to_invoke()");
    MessageHeader broadcast{foo_ptr, 0, 64, 0,     8,
                            3,       1, 0,  false, MessageType::Broadcast};
    CHECK(broadcast.is_broadcast());
    broadcast.convert_broadcast_to_invoke(17);
    CHECK(broadcast.message_type() == MessageType::Invoke);
    CHECK(broadcast.target_collection_index() == 17);

    MessageHeader broadcast_to{foo_ptr, 8, 64, 0,     8,
                               3,       1, 0,  false, MessageType::BroadcastTo};
    CHECK(broadcast_to.is_broadcast_to());
    broadcast_to.convert_broadcast_to_invoke(4321);
    CHECK(broadcast_to.message_type() == MessageType::Invoke);
    CHECK(broadcast_to.target_collection_index() == 4321);

    MessageHeader invoke{foo_ptr, 9, 64, 0,     8,
                         3,       1, 0,  false, MessageType::Invoke};
    CHECK_THROWS_WITH_AS(
        invoke.convert_broadcast_to_invoke(99),
        "Cannot convert message type Invoke to an Invoke message because we "
        "can only convert Broadcast and BroadcastTo messages.",
        Exception);
  }
  {
    const MessageHeader header{foo_ptr, 42, 256, 11,    128,
                               2,       7,  8,   false, MessageType::Broadcast};
    const std::string output = detail::get_output(header);
    CHECK(output.find("MessageHeader {") != std::string::npos);
    CHECK(output.find("member_function_ptr:") != std::string::npos);
    CHECK(output.find("target_collection_index: 42") != std::string::npos);
    CHECK(output.find("number_of_bytes_in_message: 256") != std::string::npos);
    CHECK(output.find("distributed_object_index: 11") != std::string::npos);
    CHECK(output.find("data_offset: 128") != std::string::npos);
    CHECK(output.find("source_process_id: 2") != std::string::npos);
    CHECK(output.find("destination_process_id: 7") != std::string::npos);
    CHECK(output.find("quiescence_detection_sweep_number: 8") !=
          std::string::npos);
    CHECK(output.find("data_alignment:") != std::string::npos);
    CHECK(output.find("data_was_serialized: false") != std::string::npos);
    CHECK(output.find("message_type: Broadcast") != std::string::npos);
  }

  const rts::detail::MemberFunctionPtr dummy_ptr1{};
  rts::detail::MemberFunctionPtr dummy_ptr2{};
  dummy_ptr2.lower = 1;  // Make it different

  const std::uint64_t target_collection_index = 42;
  const std::uint64_t num_bytes = sizeof(rts::MessageHeader) + 16;
  const std::uint32_t distributed_object_index = 7;
  const std::uint32_t data_offset = sizeof(rts::MessageHeader);
  const std::int32_t source_id = 1;
  const std::int32_t dest_id = 2;
  const std::uint64_t sweep = 123;
  const bool was_serialized = false;
  const rts::MessageType type = rts::MessageType::Invoke;

  const rts::MessageHeader base(
      dummy_ptr1, target_collection_index, num_bytes, distributed_object_index,
      data_offset, source_id, dest_id, sweep, was_serialized, type);

  const rts::MessageHeader identical(
      dummy_ptr1, target_collection_index, num_bytes, distributed_object_index,
      data_offset, source_id, dest_id, sweep, was_serialized, type);

  CHECK(base == identical);
  CHECK_FALSE(base != identical);

  {
    // member_function_ptr
    CHECK(base != rts::MessageHeader(dummy_ptr2, target_collection_index,
                                     num_bytes, distributed_object_index,
                                     data_offset, source_id, dest_id, sweep,
                                     was_serialized, type));
  }
  {
    // target_collection_index
    CHECK(base != rts::MessageHeader(dummy_ptr1, target_collection_index + 1,
                                     num_bytes, distributed_object_index,
                                     data_offset, source_id, dest_id, sweep,
                                     was_serialized, type));
  }
  {
    // number_of_bytes_in_message
    CHECK(base != rts::MessageHeader(dummy_ptr1, target_collection_index,
                                     num_bytes + 1, distributed_object_index,
                                     data_offset, source_id, dest_id, sweep,
                                     was_serialized, type));
  }
  {
    // distributed_object_index
    CHECK(base != rts::MessageHeader(dummy_ptr1, target_collection_index,
                                     num_bytes, distributed_object_index + 1,
                                     data_offset, source_id, dest_id, sweep,
                                     was_serialized, type));
  }
  {
    // data_offset
    CHECK(base != rts::MessageHeader(dummy_ptr1, target_collection_index,
                                     num_bytes, distributed_object_index,
                                     data_offset + 1, source_id, dest_id, sweep,
                                     was_serialized, type));
  }
  {
    // source_process_id
    CHECK(base != rts::MessageHeader(dummy_ptr1, target_collection_index,
                                     num_bytes, distributed_object_index,
                                     data_offset, source_id + 1, dest_id, sweep,
                                     was_serialized, type));
  }
  {
    // destination_process_id
    CHECK(base != rts::MessageHeader(dummy_ptr1, target_collection_index,
                                     num_bytes, distributed_object_index,
                                     data_offset, source_id, dest_id + 1, sweep,
                                     was_serialized, type));
  }
  {
    // quiescence_detection_sweep_number
    CHECK(base != rts::MessageHeader(dummy_ptr1, target_collection_index,
                                     num_bytes, distributed_object_index,
                                     data_offset, source_id, dest_id, sweep + 1,
                                     was_serialized, type));
  }
  {
    // data_alignment
    const auto deleter = [](std::byte* ptr) {
      ::operator delete[](ptr, std::align_val_t(alignof(MessageHeader)));
    };
    std::unique_ptr<std::byte[], decltype(deleter)> buffer1{
        new (std::align_val_t(alignof(MessageHeader)))
            std::byte[sizeof(rts::MessageHeader) + sizeof(double)],
        deleter};
    // std::make_unique<char[]>(sizeof(rts::MessageHeader) + alignof(double));
    auto* header1 = new (buffer1.get())
        rts::MessageHeader(dummy_ptr1, target_collection_index, num_bytes,
                           distributed_object_index, data_offset, source_id,
                           dest_id, sweep, was_serialized, type);
    rts::create_data_in_message<double>(*header1);

    std::unique_ptr<std::byte[], decltype(deleter)> buffer2{
        new (std::align_val_t(alignof(MessageHeader)))
            std::byte[sizeof(rts::MessageHeader) + sizeof(int)],
        deleter};
    auto* header2 = new (buffer2.get())
        rts::MessageHeader(dummy_ptr1, target_collection_index, num_bytes,
                           distributed_object_index, data_offset, source_id,
                           dest_id, sweep, was_serialized, type);
    rts::create_data_in_message<int>(*header2);

    CHECK_FALSE(*header1 == *header2);
    CHECK(*header1 != *header2);
  }
  {
    // data_was_serialized
    CHECK(base != rts::MessageHeader(dummy_ptr1, target_collection_index,
                                     num_bytes, distributed_object_index,
                                     data_offset, source_id, dest_id, sweep,
                                     not was_serialized, type));
  }
  {
    // message_type
    CHECK(base != rts::MessageHeader(
                      dummy_ptr1, target_collection_index, num_bytes,
                      distributed_object_index, data_offset, source_id, dest_id,
                      sweep, was_serialized, rts::MessageType::Broadcast));
  }
  {
    // Check special collection index values
    CHECK(rts::MessageHeader::no_collection_index() != 0);
    CHECK(rts::MessageHeader::reduction_message_collection_index() != 0);
    CHECK(rts::MessageHeader::no_collection_index() !=
          rts::MessageHeader::reduction_message_collection_index());
  }
}
}  // namespace rts
#endif
