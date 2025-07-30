// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/Message.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>

#include "Rts/DistributedTaskDriver.hpp"

namespace rts {
bool Message_t::execute(
    rts::ThreadPool<Message_t, rts::DistributedTaskDriver*>& /*pool*/,
    const std::uint32_t thread_id, Message_t& message,
    DistributedTaskDriver* distributed_task_driver) {
  distributed_task_driver->invoke(message, thread_id);
  return true;
}

Message_t copy(const Message_t& message) {
  const MessageHeader& message_header = *message.get_header();
  std::unique_ptr<std::byte[]> buffer{
      new (std::align_val_t(
          std::max(alignof(MessageHeader),
                   static_cast<size_t>(message_header.data_alignment()))))
          std::byte[message_header.number_of_bytes_in_message()]};
  std::memcpy(buffer.get(), message.message.get(),
              message_header.number_of_bytes_in_message());
  return {std::move(buffer)};
}
}  // namespace rts


#if defined(RTS_ENABLE_TESTING)

#include <doctest/doctest.h>

namespace rts {
namespace {
struct alignas(rts::hardware_info::hardware_destructive_interference_size)
    AlignedStruct {
  int a;
  double b;
  std::size_t c;
  char d;
};

void test_create_message_alignment_and_values() {
  INFO("Test create_message with various types and alignment");

  // Edge case: struct with large alignment and only fundamental types
  AlignedStruct struct_value{42, 3.14, 123456, 'x'};
  int int_value = -7;
  double double_value = 2.718;
  std::size_t size_t_value = 9999;

  // Prepare message arguments as a tuple
  std::tuple args_tuple{int_value, double_value, size_t_value, struct_value};

  // Dummy member function pointer and header fields
  const rts::detail::MemberFunctionPtr dummy_ptr{};
  const std::uint64_t target_collection_index = 0;
  const std::uint32_t distributed_object_index = 1;
  const std::int32_t source_process_id = 2;
  const std::int32_t destination_process_id = 3;
  const std::uint64_t sweep_number = 4;
  const bool was_serialized = false;
  const rts::MessageType message_type = rts::MessageType::Invoke;

  // Create the message
  Message_t message = create_message(
      dummy_ptr, target_collection_index, distributed_object_index,
      source_process_id, destination_process_id, sweep_number, was_serialized,
      message_type, args_tuple);

  // Check header fields
  const MessageHeader* header = message.get_header();
  CHECK(header->member_function_ptr() == dummy_ptr);
  CHECK(header->target_collection_index() == target_collection_index);
  CHECK(header->distributed_object_index() == distributed_object_index);
  CHECK(header->source_process_id() == source_process_id);
  CHECK(header->destination_process_id() == destination_process_id);
  CHECK(header->quiescence_detection_sweep_number() == sweep_number);
  CHECK(header->data_was_serialized() == was_serialized);
  CHECK(header->message_type() == message_type);

  // Edge case: check alignment of the data
  const void* data_ptr = header->data_location();
  CHECK(reinterpret_cast<std::uintptr_t>(data_ptr) %
            alignof(std::tuple<int, double, std::size_t, AlignedStruct>) ==
        0);

  // Edge case: check buffer size is sufficient for alignment and data
  const std::size_t expected_data_offset =
      sizeof(MessageHeader) +
      (alignof(std::tuple<int, double, std::size_t, AlignedStruct>) -
       sizeof(MessageHeader) %
           alignof(std::tuple<int, double, std::size_t, AlignedStruct>));
  const std::size_t expected_buffer_size =
      expected_data_offset +
      sizeof(std::tuple<int, double, std::size_t, AlignedStruct>);
  CHECK(header->number_of_bytes_in_message() == expected_buffer_size);

  // Check that the data is correctly stored and retrievable
  using DataTuple = std::tuple<int, double, std::size_t, AlignedStruct>;
  const DataTuple* data =
      reinterpret_cast<const DataTuple*>(header->data_location());
  CHECK(std::get<0>(*data) == int_value);
  CHECK(std::get<1>(*data) == double_value);
  CHECK(std::get<2>(*data) == size_t_value);

  // Check struct values
  const AlignedStruct& struct_from_msg = std::get<3>(*data);
  CHECK(struct_from_msg.a == struct_value.a);
  CHECK(struct_from_msg.b == struct_value.b);
  CHECK(struct_from_msg.c == struct_value.c);
  CHECK(struct_from_msg.d == struct_value.d);
}

void test_copy_message() {
  INFO("Test Copy Message_t");
  // Setup a dummy MessageHeader
  const rts::detail::MemberFunctionPtr dummy_ptr{};
  const std::uint64_t target_collection_index = 42;
  const std::uint64_t num_bytes = sizeof(rts::MessageHeader) + 16;
  const std::uint32_t distributed_object_index = 7;
  const std::uint32_t data_offset = sizeof(rts::MessageHeader);
  const std::int32_t source_id = 1;
  const std::int32_t dest_id = 2;
  const std::uint64_t sweep = 123;
  const bool was_serialized = false;
  const rts::MessageType type = rts::MessageType::Invoke;

  // Allocate buffer for message
  std::unique_ptr<std::byte[]> buffer(new std::byte[num_bytes]);
  // Placement new for header
  const MessageHeader* header = new (buffer.get()) rts::MessageHeader(
      dummy_ptr, target_collection_index, num_bytes, distributed_object_index,
      data_offset, source_id, dest_id, sweep, was_serialized, type);

  // Fill payload with known pattern
  std::byte* const payload = buffer.get() + data_offset;
  for (size_t i = 0; i < 16; ++i) {
    payload[i] = static_cast<std::byte>(i + 10);
  }

  // Create the message
  Message_t message;
  message.message = std::move(buffer);

  // Copy the message
  Message_t copied = copy(message);

  // Check header fields
  const MessageHeader* copied_header = copied.get_header();
  CHECK((*copied_header) == (*header));

  // Check payload
  const std::byte* const copied_payload = copied.message.get() + data_offset;
  for (size_t i = 0; i < 16; ++i) {
    CHECK(copied_payload[i] == static_cast<std::byte>(i + 10));
  }
}
}  // namespace
}  // namespace rts

TEST_CASE("Message") {
  rts::test_create_message_alignment_and_values();
  rts::test_copy_message();
}

#endif
