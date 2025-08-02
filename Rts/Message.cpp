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

Message_t create_broadcast_to_message(
    const rts::detail::MemberFunctionPtr& member_function_ptr,
    const std::uint32_t distributed_object_index,
    const std::int32_t source_process_id,
    const std::int32_t destination_process_id,
    const std::uint64_t quiescence_detection_sweep_number,
    const bool was_serialized, const int number_of_elements_on_pid,
    const std::uint64_t data_alignment, const std::uint64_t data_size,
    const void* const data_ptr) {
  // Extra metadata: [target_pid, num_elements, collection_indices...]
  const std::uint32_t extra_metadata_bytes =
      sizeof(std::uint64_t) *
      (2 + static_cast<std::uint32_t>(number_of_elements_on_pid));

  const std::uint32_t header_size = sizeof(rts::MessageHeader);
  const std::uint32_t data_offset =
      header_size +
      extra_metadata_bytes
      // Add extra bytes to make sure we can align Data_t
      // properly. We compute the remainder of the MessageHeader size and
      // the alignment of the data. This would give us, e.g. 5 bytes, which
      // means we have e.g. 37 bytes for MessageHeader. The amount we
      // would need to align then is given by the C++:
      +
      (data_alignment - (header_size + extra_metadata_bytes) % data_alignment);

  const std::uint64_t buffer_size = data_offset + data_size;

  std::unique_ptr<std::byte[]> buffer(new (std::align_val_t(std::max(
      alignof(MessageHeader), data_alignment))) std::byte[buffer_size]);

  // Placement-new construct the header at the start of the buffer
  MessageHeader* header_ptr = new (buffer.get())
      MessageHeader(member_function_ptr,
                    // collection index is not used for broadcast_to
                    MessageHeader::no_collection_index(), buffer_size,
                    distributed_object_index, data_offset, source_process_id,
                    destination_process_id, quiescence_detection_sweep_number,
                    was_serialized, rts::MessageType::BroadcastTo);

  // Write extra metadata after the header for the PIDs
  std::uint64_t* meta_ptr =
      reinterpret_cast<std::uint64_t*>(buffer.get() + header_size);
  meta_ptr[0] = static_cast<std::uint64_t>(destination_process_id);
  meta_ptr[1] = 0;

  // Copy the data tuple into the correct location using memcpy
  std::memcpy(header_ptr->data_location(), data_ptr, data_size);

  // Optionally, set the data alignment in the header (if needed)
  header_ptr->set_data_alignment(data_alignment);

  return {std::move(buffer)};
}

Message_t create_local_invoke_message(
    const rts::detail::MemberFunctionPtr& member_function_ptr,
    const std::uint64_t collection_index,
    const std::uint32_t distributed_object_index,
    const std::int32_t source_process_id,
    const std::int32_t destination_process_id,
    const std::uint64_t quiescence_detection_sweep_number,
    const bool was_serialized, const std::uint64_t data_alignment,
    const std::uint64_t data_size, const void* const data_ptr) {
  const std::uint32_t header_size = sizeof(rts::MessageHeader);
  const std::uint32_t data_offset =
      header_size + (data_alignment - (header_size % data_alignment));
  const std::uint64_t buffer_size = data_offset + data_size;

  std::unique_ptr<std::byte[]> buffer(new (std::align_val_t(std::max(
      alignof(rts::MessageHeader), data_alignment))) std::byte[buffer_size]);

  // Placement-new construct the header at the start of the buffer
  rts::MessageHeader* const header_ptr = new (buffer.get()) rts::MessageHeader(
      member_function_ptr, collection_index, buffer_size,
      distributed_object_index, data_offset, source_process_id,
      destination_process_id, quiescence_detection_sweep_number, was_serialized,
      rts::MessageType::Invoke);

  // Copy the data tuple into the correct location using memcpy
  std::memcpy(header_ptr->data_location(), data_ptr, data_size);

  // Set the data alignment in the header
  header_ptr->set_data_alignment(data_alignment);

  return {std::move(buffer)};
}

namespace reduction {
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
}  // namespace reduction
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

/*
 * \brief Test create_broadcast_to_message for correct header, alignment,
 *        metadata, and data copying.
 */
void test_create_broadcast_to_message() {
  INFO("Test create_broadcast_to_message with metadata and alignment");

  // Define a data tuple type and value
  using DataTuple = std::tuple<int, double, char>;
  const DataTuple data_value{42, 3.14, 'z'};

  // Prepare dummy member function pointer and header fields
  const rts::detail::MemberFunctionPtr dummy_ptr{};
  const std::uint32_t distributed_object_index = 5;
  const std::int32_t source_process_id = 1;
  const std::int32_t destination_process_id = 2;
  const std::uint64_t sweep_number = 123;
  const bool was_serialized = false;
  const int number_of_elements_on_pid = 3;
  const std::uint64_t data_alignment = alignof(DataTuple);
  const std::uint64_t data_size = sizeof(DataTuple);

  // Create the message
  Message_t message = create_broadcast_to_message(
      dummy_ptr, distributed_object_index, source_process_id,
      destination_process_id, sweep_number, was_serialized,
      number_of_elements_on_pid, data_alignment, data_size, &data_value);

  // Check header fields
  const MessageHeader* header = message.get_header();
  CHECK(header->member_function_ptr() == dummy_ptr);
  CHECK(header->distributed_object_index() == distributed_object_index);
  CHECK(header->source_process_id() == source_process_id);
  CHECK(header->destination_process_id() == destination_process_id);
  CHECK(header->quiescence_detection_sweep_number() == sweep_number);
  CHECK(header->data_was_serialized() == was_serialized);
  CHECK(header->message_type() == MessageType::BroadcastTo);
  CHECK(header->data_alignment() == data_alignment);

  // Check extra metadata: [target_pid, num_elements, ...]
  const std::uint32_t header_size = sizeof(MessageHeader);
  const std::uint64_t* meta_ptr = reinterpret_cast<const std::uint64_t*>(
      reinterpret_cast<const char*>(header) + header_size);
  CHECK(meta_ptr[0] == static_cast<std::uint64_t>(destination_process_id));
  CHECK(meta_ptr[1] == 0);

  // Check alignment of the data
  const void* data_ptr = header->data_location();
  CHECK(reinterpret_cast<std::uintptr_t>(data_ptr) % alignof(DataTuple) == 0);

  // Check that the data is correctly copied
  const DataTuple* data_from_msg = reinterpret_cast<const DataTuple*>(data_ptr);
  CHECK(*data_from_msg == data_value);

  // Check buffer size is sufficient for alignment and data
  const std::uint32_t expected_extra_metadata_bytes =
      sizeof(std::uint64_t) *
      (2 + static_cast<std::uint32_t>(number_of_elements_on_pid));
  const std::size_t expected_data_offset =
      header_size + expected_extra_metadata_bytes +
      (data_alignment -
       (header_size + expected_extra_metadata_bytes) % data_alignment);
  const std::size_t expected_buffer_size = expected_data_offset + data_size;
  CHECK(header->number_of_bytes_in_message() == expected_buffer_size);
}

void test_create_local_invoke_message() {
  INFO(
      "Test create_local_invoke_message for correct header, alignment, and "
      "data");

  // Define a data tuple type and value
  using DataTuple = std::tuple<int, double, char>;
  const DataTuple data_value{123, 4.56, 'a'};

  // Prepare dummy member function pointer and header fields
  const rts::detail::MemberFunctionPtr dummy_ptr{};
  const std::uint64_t collection_index = 99;
  const std::uint32_t distributed_object_index = 7;
  const std::int32_t source_process_id = 1;
  const std::int32_t destination_process_id = 2;
  const std::uint64_t sweep_number = 321;
  const bool was_serialized = false;
  const std::uint64_t data_alignment = alignof(DataTuple);
  const std::uint64_t data_size = sizeof(DataTuple);

  // Create the message
  Message_t message = create_local_invoke_message(
      dummy_ptr, collection_index, distributed_object_index, source_process_id,
      destination_process_id, sweep_number, was_serialized, data_alignment,
      data_size, &data_value);

  // Check header fields
  const MessageHeader* header = message.get_header();
  CHECK(header->member_function_ptr() == dummy_ptr);
  CHECK(header->target_collection_index() == collection_index);
  CHECK(header->distributed_object_index() == distributed_object_index);
  CHECK(header->source_process_id() == source_process_id);
  CHECK(header->destination_process_id() == destination_process_id);
  CHECK(header->quiescence_detection_sweep_number() == sweep_number);
  CHECK(header->data_was_serialized() == was_serialized);
  CHECK(header->message_type() == MessageType::Invoke);
  CHECK(header->data_alignment() == data_alignment);

  // Check alignment of the data
  const void* data_ptr = header->data_location();
  CHECK(reinterpret_cast<std::uintptr_t>(data_ptr) % alignof(DataTuple) == 0);

  // Check that the data is correctly copied
  const DataTuple* data_from_msg = reinterpret_cast<const DataTuple*>(data_ptr);
  CHECK(*data_from_msg == data_value);

  // Check buffer size is sufficient for alignment and data
  const std::uint32_t header_size = sizeof(MessageHeader);
  const std::size_t expected_data_offset =
      header_size + (data_alignment - (header_size % data_alignment));
  const std::size_t expected_buffer_size = expected_data_offset + data_size;
  CHECK(header->number_of_bytes_in_message() == expected_buffer_size);
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

namespace reduction {
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
}  // namespace
}  // namespace reduction
}  // namespace rts

TEST_CASE("Message") {
  rts::test_create_message_alignment_and_values();
  rts::test_create_broadcast_to_message();
  rts::test_create_local_invoke_message();
  rts::test_copy_message();

  rts::reduction::test_set_and_get_id();
  rts::reduction::test_set_and_get_data_offset();
  rts::reduction::test_set_and_get_callback_offset();
}

#endif
