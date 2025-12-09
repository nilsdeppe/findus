// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "findus/Message.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <ostream>
#include <utility>

#include "findus/DistributedTaskDriver.hpp"
#include "findus/MessageHeader.hpp"
#include "findus/Serialize/Serializer.hpp"

namespace findus {
MessageRequeue Message_t::execute(
    findus::ThreadPool<Message_t, findus::DistributedTaskDriver*>& /*pool*/,
    const std::uint32_t thread_id, Message_t& message,
    DistributedTaskDriver* distributed_task_driver) {
  return distributed_task_driver->invoke(message, thread_id);
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
    const findus::detail::MemberFunctionPtr& member_function_ptr,
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

  const std::uint32_t header_size = sizeof(findus::MessageHeader);
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
                    was_serialized, findus::MessageType::BroadcastTo);

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

Message_t create_message(
    const findus::detail::MemberFunctionPtr& member_function_ptr,
    const std::uint64_t collection_index,
    const std::uint32_t distributed_object_index,
    const std::int32_t source_process_id,
    const std::int32_t destination_process_id,
    const std::uint64_t quiescence_detection_sweep_number,
    const bool was_serialized, const MessageType message_type,
    const std::uint64_t data_alignment, const std::uint64_t data_size,
    const void* const data_ptr) {
  const std::uint32_t header_size = sizeof(findus::MessageHeader);
  const std::uint32_t data_offset =
      header_size + (data_alignment - (header_size % data_alignment));
  const std::uint64_t buffer_size = data_offset + data_size;

  std::unique_ptr<std::byte[]> buffer(new (std::align_val_t(std::max(
      alignof(findus::MessageHeader), data_alignment))) std::byte[buffer_size]);

  // Placement-new construct the header at the start of the buffer
  findus::MessageHeader* const header_ptr = new (buffer.get())
      findus::MessageHeader(member_function_ptr, collection_index, buffer_size,
                            distributed_object_index, data_offset,
                            source_process_id, destination_process_id,
                            quiescence_detection_sweep_number, was_serialized,
                            message_type);

  // Copy the data tuple into the correct location using memcpy
  std::memcpy(header_ptr->data_location(), data_ptr, data_size);

  // Set the data alignment in the header
  header_ptr->set_data_alignment(data_alignment);

  return {std::move(buffer)};
}

namespace reduction {
static constexpr std::ptrdiff_t reduction_id_offset_in_bytes =
    sizeof(MessageHeader);
static constexpr std::ptrdiff_t data_offset_jump_in_bytes =
    8 + reduction_id_offset_in_bytes;
static constexpr std::ptrdiff_t callback_offset_jump_in_bytes =
    data_offset_jump_in_bytes + 4;
static constexpr std::ptrdiff_t combine_offset_jump_in_bytes =
    callback_offset_jump_in_bytes + 4;
static constexpr std::ptrdiff_t data_size_jump_in_bytes =
    combine_offset_jump_in_bytes + 8;
static constexpr std::ptrdiff_t contributed_metadata_jump_in_bytes =
    data_size_jump_in_bytes + 4;
static constexpr std::ptrdiff_t target_parent_jump_in_bytes =
    contributed_metadata_jump_in_bytes + 4;
static constexpr std::ptrdiff_t expected_contributions_jump_in_bytes =
    target_parent_jump_in_bytes + 4;
static constexpr std::ptrdiff_t root_expected_contributions_jump_in_bytes =
    expected_contributions_jump_in_bytes + 4;
// Note: since the contributed_metadata is only 8 bits (1 byte), the "next"
// thing would no longer be 8-byte aligned, so we probably want to push this
// metadata to always be the last thing we store.
//
// The layout of the metadata in terms of bytes (from right to left)
// 1. self has contributed (& 0b1)
// 2. left child has contributed (>> 1 & 0b1)
// 3. right child has contributed (>> 2 & 0b1)
// 4. parent has local contributions (>> 3 & 0b1)
// 5. parent's other child should contribute [only set if parent has local
//    isn't set] (>> 4 & 0b1)

static_assert(contributed_metadata_jump_in_bytes -
                  reduction_id_offset_in_bytes <=
              metadata_block_size);

void set_id(Message_t& message, const std::uint64_t reduction_id) {
  static_assert(reduction_id_offset_in_bytes + alignof(MessageHeader) >=
                alignof(std::uint64_t));
  *reinterpret_cast<std::uint64_t*>(
      std::next(reinterpret_cast<std::byte*>(message.get_header()),
                reduction_id_offset_in_bytes)) = reduction_id;
}

std::uint64_t get_id(const Message_t& message) {
  return *reinterpret_cast<const std::uint64_t*>(
      std::next(reinterpret_cast<const std::byte*>(message.get_header()),
                reduction_id_offset_in_bytes));
}

void set_data_offset(Message_t& message, const std::uint32_t data_offset) {
  static_assert(data_offset_jump_in_bytes + alignof(MessageHeader) >=
                alignof(std::uint32_t));
  *reinterpret_cast<std::uint32_t*>(
      std::next(reinterpret_cast<std::byte*>(message.get_header()),
                data_offset_jump_in_bytes)) = data_offset;
}

std::uint32_t get_data_offset(const Message_t& message) {
  return *reinterpret_cast<const std::uint32_t*>(
      std::next(reinterpret_cast<const std::byte*>(message.get_header()),
                data_offset_jump_in_bytes));
}

void set_callback_offset(Message_t& message,
                         const std::uint32_t callback_offset) {
  static_assert(callback_offset_jump_in_bytes + alignof(MessageHeader) >=
                alignof(std::uint32_t));
  *reinterpret_cast<std::uint32_t*>(
      std::next(reinterpret_cast<std::byte*>(message.get_header()),
                callback_offset_jump_in_bytes)) = callback_offset;
}

std::uint32_t get_callback_offset(const Message_t& message) {
  return *reinterpret_cast<const std::uint32_t*>(
      std::next(reinterpret_cast<const std::byte*>(message.get_header()),
                callback_offset_jump_in_bytes));
}

std::byte* get_callback_address(Message_t& message) {
  return std::next(reinterpret_cast<std::byte*>(message.get_header()),
                   get_callback_offset(message));
}

const std::byte* get_callback_address(const Message_t& message) {
  return std::next(reinterpret_cast<const std::byte*>(message.get_header()),
                   get_callback_offset(message));
}

namespace {
void combine_anchor(Message_t&, const Message_t&) {}

using combine_function_ptr_t = void (*)(Message_t&, const Message_t&);

// helper function used to silence acceptable integer overflow.
template <bool Add>
__attribute__((no_sanitize("undefined"
#if defined(__clang__)
                           ,
                           "integer"
#endif
                           ))) std::uint64_t
combine_uint64(const std::uint64_t lhs, const std::uint64_t rhs) noexcept {
  if constexpr (Add) {
    return lhs + rhs;
  } else {
    return lhs - rhs;
  }
}
}  // namespace

void set_combine_function_pointer(Message_t& message,
                                  void (*pointer)(Message_t&,
                                                  const Message_t&)) {
  static_assert(sizeof(std::uint64_t) == sizeof(pointer),
                "Internal error. Please file a bug report.");
  union PtrConversionUnion {
    combine_function_ptr_t f;
    std::uint64_t bits;
  };
  PtrConversionUnion f_ptr{pointer};
  PtrConversionUnion anchor_ptr{&combine_anchor};
  *reinterpret_cast<std::uint64_t*>(
      std::next(reinterpret_cast<std::byte*>(message.get_header()),
                combine_offset_jump_in_bytes)) =
      combine_uint64<false>(f_ptr.bits, anchor_ptr.bits);
}

auto get_combine_function_pointer(const Message_t& message)
    -> void (*)(Message_t&, const Message_t&) {
  union PtrConversionUnion {
    combine_function_ptr_t f;
    std::uint64_t bits;
  };
  PtrConversionUnion anchor_ptr{&combine_anchor};
  PtrConversionUnion f_ptr{};
  f_ptr.bits = combine_uint64<true>(
      *reinterpret_cast<const std::uint64_t*>(
          std::next(reinterpret_cast<const std::byte*>(message.get_header()),
                    combine_offset_jump_in_bytes)),
      anchor_ptr.bits);
  return f_ptr.f;
}

void set_data_size(Message_t& message, const std::uint32_t data_size) {
  *reinterpret_cast<std::uint32_t*>(
      std::next(reinterpret_cast<std::byte*>(message.get_header()),
                data_size_jump_in_bytes)) = data_size;
}

std::uint32_t get_data_size(const Message_t& message) {
  return *reinterpret_cast<const std::uint32_t*>(
      std::next(reinterpret_cast<const std::byte*>(message.get_header()),
                data_size_jump_in_bytes));
}

std::ostream& operator<<(std::ostream& os, const Contribution contribution) {
  switch (contribution) {
    case Contribution::self_contributed:
      return os << "self_contributed";
    case Contribution::left_child_contributed:
      return os << "left_child_contributed";
    case Contribution::right_child_contributed:
      return os << "right_child_contributed";
    case Contribution::parent_has_local_contributions:
      return os << "parent_has_local_contributions";
    case Contribution::parents_other_child_has_contributions:
      return os << "parents_other_child_has_contributions";
    default:
      return os << "Unknown";
  }
}

bool message_ready(const Message_t& message) {
  return std::all_of(core_contributed.begin(), core_contributed.end(),
                     [&message](const Contribution cont) {
                       return get_contributed_metadata(message, cont);
                     });
}

void zero_contributed_metadata(Message_t& message) {
  *reinterpret_cast<std::uint8_t*>(
      std::next(reinterpret_cast<std::byte*>(message.get_header()),
                contributed_metadata_jump_in_bytes)) = 0;
}

void set_contributed_metadata(Message_t& message,
                              const Contribution contribution) {
  *reinterpret_cast<std::uint8_t*>(
      std::next(reinterpret_cast<std::byte*>(message.get_header()),
                contributed_metadata_jump_in_bytes)) |=
      static_cast<std::uint8_t>(contribution);
}

void unset_contributed_metadata(Message_t& message,
                                const Contribution contribution) {
  *reinterpret_cast<std::uint8_t*>(
      std::next(reinterpret_cast<std::byte*>(message.get_header()),
                contributed_metadata_jump_in_bytes)) &=
      ~static_cast<std::uint8_t>(contribution);
}

bool get_contributed_metadata(const Message_t& message,
                              const Contribution contribution) {
  return static_cast<bool>(
      *reinterpret_cast<const std::uint8_t*>(
          std::next(reinterpret_cast<const std::byte*>(message.get_header()),
                    contributed_metadata_jump_in_bytes)) &
      static_cast<std::uint8_t>(contribution));
}

void set_target_process_id(Message_t& message,
                           const std::int32_t target_process_id) {
  *reinterpret_cast<std::int32_t*>(
      std::next(reinterpret_cast<std::byte*>(message.get_header()),
                target_parent_jump_in_bytes)) = target_process_id;
}

std::int32_t get_target_process_id(const Message_t& message) {
  return *reinterpret_cast<const std::int32_t*>(
      std::next(reinterpret_cast<const std::byte*>(message.get_header()),
                target_parent_jump_in_bytes));
}

void set_expected_number_of_contributions(
    Message_t& message, const std::int32_t expected_number_of_contributions) {
  *reinterpret_cast<std::int32_t*>(std::next(
      reinterpret_cast<std::byte*>(message.get_header()),
      expected_contributions_jump_in_bytes)) = expected_number_of_contributions;
}

std::int32_t get_expected_number_of_contributions(const Message_t& message) {
  return *reinterpret_cast<const std::int32_t*>(
      std::next(reinterpret_cast<const std::byte*>(message.get_header()),
                expected_contributions_jump_in_bytes));
}

void set_expected_number_of_root_contributions(
    Message_t& message,
    const std::int32_t expected_number_of_root_contributions) {
  *reinterpret_cast<std::int32_t*>(
      std::next(reinterpret_cast<std::byte*>(message.get_header()),
                root_expected_contributions_jump_in_bytes)) =
      expected_number_of_root_contributions;
}

std::int32_t get_expected_number_of_root_contributions(
    const Message_t& message) {
  return *reinterpret_cast<const std::int32_t*>(
      std::next(reinterpret_cast<const std::byte*>(message.get_header()),
                root_expected_contributions_jump_in_bytes));
}
}  // namespace reduction
}  // namespace findus

#if defined(FINDUS_ENABLE_TESTING)

#include <bitset>
#include <doctest/doctest.h>

#include "findus/DistributedObjectCollection.hpp"
#include "findus/Reduction.hpp"
#include "findus/Serialize/Stl/Vector.hpp"

namespace findus {
namespace {
struct alignas(findus::hardware_info::hardware_destructive_interference_size)
    AlignedStruct {
  int a;
  double b;
  std::size_t c;
  char d;
};

void test_create_message_alignment_and_values() {
  INFO("Test create_message with various types and alignment");
  using DataTuple = std::tuple<int, double, std::size_t, AlignedStruct>;

  // Edge case: struct with large alignment and only fundamental types
  AlignedStruct struct_value{42, 3.14, 123456, 'x'};
  int int_value = -7;
  double double_value = 2.718;
  std::size_t size_t_value = 9999;

  // Prepare message arguments as a tuple
  DataTuple args_tuple{int_value, double_value, size_t_value, struct_value};

  // Dummy member function pointer and header fields
  const findus::detail::MemberFunctionPtr dummy_ptr{};
  const std::uint64_t target_collection_index = 0;
  const std::uint32_t distributed_object_index = 1;
  const std::int32_t source_process_id = 2;
  const std::int32_t destination_process_id = 3;
  const std::uint64_t sweep_number = 4;
  const bool was_serialized = false;
  const findus::MessageType message_type = findus::MessageType::Invoke;

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
  CHECK(reinterpret_cast<std::uintptr_t>(data_ptr) % alignof(DataTuple) == 0);

  // Edge case: check buffer size is sufficient for alignment and data
  const std::size_t expected_data_offset =
      sizeof(MessageHeader) +
      (alignof(DataTuple) - sizeof(MessageHeader) % alignof(DataTuple));
  const std::size_t expected_buffer_size =
      expected_data_offset + sizeof(DataTuple);
  CHECK(header->number_of_bytes_in_message() == expected_buffer_size);

  // Check that the data is correctly stored and retrievable
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

void test_create_message_serialized() {
  INFO("Test create_message with serialized data");
  using DataTuple = std::tuple<std::vector<int>>;

  // Edge case: struct with large alignment and only fundamental types
  std::vector<int> vec_value{1, 2, 4, 6, 7, 9};

  // Prepare message arguments as a tuple
  std::tuple<std::vector<int>&> args_tuple{vec_value};
  const size_t packed_size = [&args_tuple]() {
    serialize::Serializer sizer{serialize::Serializer::Sizing};
    sizer | args_tuple;
    return sizer.number_of_bytes();
  }();

  // Dummy member function pointer and header fields
  const findus::detail::MemberFunctionPtr dummy_ptr{};
  const std::uint64_t target_collection_index = 0;
  const std::uint32_t distributed_object_index = 1;
  const std::int32_t source_process_id = 2;
  const std::int32_t destination_process_id = 3;
  const std::uint64_t sweep_number = 4;
  const bool was_serialized = true;
  const findus::MessageType message_type = findus::MessageType::Invoke;

  {
    const std::string msg =
        "Internal error: when calling create_message with "
        "was_serialized==false you must pass in the tuple by value "
        "arguments, not references. That is, "
        "std::is_same_v<std::decay_t<Args>,Args> is true for all Args.";
    CHECK_THROWS_WITH_AS(
        create_message(dummy_ptr, target_collection_index,
                       distributed_object_index, source_process_id,
                       destination_process_id, sweep_number, false,
                       message_type, args_tuple),
        msg.c_str(), findus::Exception);
  }

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
            alignof(std::tuple<std::vector<int>>) ==
        0);

  // Edge case: check buffer size is sufficient for alignment and data
  const std::size_t expected_data_offset =
      sizeof(MessageHeader) +
      (alignof(DataTuple) - sizeof(MessageHeader) % alignof(DataTuple));
  const std::size_t expected_buffer_size = expected_data_offset + packed_size;
  CHECK(header->number_of_bytes_in_message() == expected_buffer_size);

  // Check that the data is correctly stored and retrievable
  serialize::Serializer unpacker{
      serialize::Serializer::Unpacking,
      const_cast<std::byte*>(
          reinterpret_cast<const std::byte*>(header->data_location())),
      header->number_of_bytes_in_message() - header->data_offset()};
  DataTuple data{};
  unpacker | data;
  CHECK(vec_value == std::get<0>(data));
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
  const findus::detail::MemberFunctionPtr dummy_ptr{};
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

void test_create_message() {
  INFO(
      "Test create_local_invoke_message for correct header, alignment, and "
      "data");

  // Define a data tuple type and value
  using DataTuple = std::tuple<int, double, char>;
  const DataTuple data_value{123, 4.56, 'a'};

  // Prepare dummy member function pointer and header fields
  const findus::detail::MemberFunctionPtr dummy_ptr{};
  const std::uint64_t collection_index = 99;
  const std::uint32_t distributed_object_index = 7;
  const std::int32_t source_process_id = 1;
  const std::int32_t destination_process_id = 2;
  const std::uint64_t sweep_number = 321;
  const bool was_serialized = false;
  const std::uint64_t data_alignment = alignof(DataTuple);
  const std::uint64_t data_size = sizeof(DataTuple);

  // Create the message
  Message_t message = create_message(
      dummy_ptr, collection_index, distributed_object_index, source_process_id,
      destination_process_id, sweep_number, was_serialized,
      findus::MessageType::Invoke, data_alignment, data_size, &data_value);

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
  const findus::detail::MemberFunctionPtr dummy_ptr{};
  const std::uint64_t target_collection_index = 42;
  const std::uint64_t num_bytes = sizeof(findus::MessageHeader) + 16;
  const std::uint32_t distributed_object_index = 7;
  const std::uint32_t data_offset = sizeof(findus::MessageHeader);
  const std::int32_t source_id = 1;
  const std::int32_t dest_id = 2;
  const std::uint64_t sweep = 123;
  const bool was_serialized = false;
  const findus::MessageType type = findus::MessageType::Invoke;

  // Allocate buffer for message
  std::unique_ptr<std::byte[]> buffer(
      new (std::align_val_t(alignof(MessageHeader))) std::byte[num_bytes]);
  // Placement new for header
  const MessageHeader* header = new (buffer.get()) findus::MessageHeader(
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

void test_set_and_get_id() {
  INFO("Test set_id and get_id");
  using DataTuple = std::tuple<int, double>;
  using CallbackType = ReductionCallback<DummyAction, DummyComponent>;

  const std::uint32_t distributed_object_index = 42;
  const std::uint64_t reduction_id = 123456789;
  const DataTuple data_tuple{7, 3.14};
  const CallbackType callback{99};

  Message_t message = create_message<DummyAction, DummyComponent>(
      distributed_object_index, reduction_id, data_tuple, callback,
      MessageType::Reduction, false);
  // Use a base-10 integer for the dummy reduction ID
  const std::uint64_t dummy_id = 1234567890123456789;
  set_id(message, dummy_id);
  CHECK(get_id(message) == dummy_id);
}

void test_set_and_get_data_offset() {
  INFO("Test set_data_offset and get_data_offset");
  using DataTuple = std::tuple<int, double>;
  using CallbackType = ReductionCallback<DummyAction, DummyComponent>;

  const std::uint32_t distributed_object_index = 42;
  const std::uint64_t reduction_id = 123456789;
  const DataTuple data_tuple{7, 3.14};
  const CallbackType callback{99};

  Message_t message = create_message<DummyAction, DummyComponent>(
      distributed_object_index, reduction_id, data_tuple, callback,
      MessageType::Reduction, false);
  CHECK(get_data_pointer<DataTuple>(message) ==
        get_data_pointer<DataTuple>(std::as_const(message)));
  CHECK(*get_data_pointer<DataTuple>(message) ==
        *get_data_pointer<DataTuple>(std::as_const(message)));
  CHECK_THROWS_WITH_AS(
      get_data_pointer<std::tuple<int>>(message),
      "Cannot convert the data to the requested type because the internal "
      "size 16 does not match the size of the type 4",
      findus::Exception);
  message.get_header()->data_was_serialized(true);
  CHECK_THROWS_WITH_AS(
      get_data_pointer<DataTuple>(message),
      "Cannot retrieve the data pointer with a type other than std::byte "
      "for a serialized message.",
      findus::Exception);
  message.get_header()->data_was_serialized(false);

  const std::uint32_t dummy_offset = 305419896;
  const std::uint32_t dummy_size = 182739;
  set_data_offset(message, dummy_offset);
  CHECK(get_data_offset(message) == dummy_offset);
  CHECK(get_data_pointer<std::byte>(message) ==
        get_data_pointer<std::byte>(std::as_const(message)));
  CHECK(get_data_pointer<std::byte>(message) ==
        std::next(reinterpret_cast<std::byte*>(message.get_header()),
                  dummy_offset));
  set_data_size(message, dummy_size);
  CHECK(get_data_size(message) == dummy_size);
}

void test_set_and_get_callback_offset() {
  INFO("Test set_callback_offset and get_callback_offset");
  using DataTuple = std::tuple<int, double>;
  using CallbackType = ReductionCallback<DummyAction, DummyComponent>;

  const std::uint32_t distributed_object_index = 42;
  const std::uint64_t reduction_id = 123456789;
  const DataTuple data_tuple{7, 3.14};
  const CallbackType callback{99};

  Message_t message = create_message<DummyAction, DummyComponent>(
      distributed_object_index, reduction_id, data_tuple, callback,
      MessageType::Reduction, false);
  const std::uint32_t dummy_offset = 987654321;
  set_callback_offset(message, dummy_offset);
  CHECK(get_callback_offset(message) == dummy_offset);
}

void test_create_message() {
  using DataTuple = std::tuple<std::vector<int>, std::vector<double>>;
  using CallbackType = ReductionCallback<DummyAction, DummyComponent>;

  const std::uint32_t distributed_object_index = 42;
  const std::uint64_t reduction_id = 123456789;
  const DataTuple data_tuple{std::vector<int>{7},
                             std::vector<double>{3.14, 2.03}};
  const CallbackType callback{99};

  for (const bool serialize : {false, true}) {
    CAPTURE(serialize);
    // Create the message
    const Message_t message =
        create_message(distributed_object_index, reduction_id, data_tuple,
                       callback, MessageType::Reduction, serialize);
    CHECK(message.get_header()->data_was_serialized() == serialize);

    // Check metadata was set correctly.
    CHECK(get_id(message) == reduction_id);
    CHECK(get_data_offset(message) ==
          (message.get_header()->data_location() -
           reinterpret_cast<const char*>(message.get_header())));
    CHECK(get_callback_offset(message) > get_data_offset(message));
    // Strictly less than because zero-size objects aren't allowed in C++
    CHECK(get_callback_offset(message) <
          message.get_header()->number_of_bytes_in_message());

    // Check header
    const MessageHeader* header = message.get_header();
    CHECK(header != nullptr);
    CHECK(header->distributed_object_index() == distributed_object_index);
    CHECK(header->message_type() == MessageType::Reduction);
    CHECK(header->data_alignment() == alignof(DataTuple));

    {
      // Check metadata block is zeroed. We may stop zeroing in the future for
      // better efficiency.
      const std::byte* metadata_ptr =
          reinterpret_cast<const std::byte*>(header);
      for (std::size_t i = (callback_offset_jump_in_bytes + 4);
           i < metadata_block_size; ++i) {
        CAPTURE(i);
        CHECK(std::to_integer<unsigned char>(metadata_ptr[i]) == 0);
      }
    }

    CHECK(header->target_collection_index() ==
          MessageHeader::reduction_message_collection_index());

    if (serialize) {
      const std::byte* data_ptr = get_data_pointer<std::byte>(message);
      CHECK(reinterpret_cast<const char*>(data_ptr) == header->data_location());
      CHECK(data_ptr != nullptr);

      DataTuple result{};
      serialize::Serializer unpacker{serialize::Serializer::Unpacking, data_ptr,
                                     get_data_size(message)};
      unpacker | result;

      CHECK(std::get<0>(result) == std::get<0>(data_tuple));
      CHECK(std::get<1>(result) == std::get<1>(data_tuple));
    } else {
      const DataTuple* data_ptr = get_data_pointer<DataTuple>(message);
      CHECK(reinterpret_cast<const char*>(data_ptr) == header->data_location());
      CHECK(data_ptr != nullptr);
      CHECK(std::get<0>(*data_ptr) == std::get<0>(data_tuple));
      CHECK(std::get<1>(*data_ptr) == std::get<1>(data_tuple));
      CHECK(reinterpret_cast<std::uintptr_t>(data_ptr) % alignof(DataTuple) ==
            0);
    }

    // Check callback
    const std::uint32_t callback_offset = get_callback_offset(message);
    const CallbackType* callback_ptr = reinterpret_cast<const CallbackType*>(
        reinterpret_cast<const std::byte*>(header) + callback_offset);
    CHECK(callback_ptr != nullptr);
    CHECK(*callback_ptr == callback);

    // Check alignment
    CHECK(reinterpret_cast<std::uintptr_t>(callback_ptr) % callback_alignment ==
          0);
  }
}

void test_get_callback_address_and_get_callback() {
  using DataTuple = std::tuple<int, double>;
  using CallbackType = ReductionCallback<DummyAction, DummyComponent>;

  const std::uint32_t distributed_object_index = 7;
  const std::uint64_t reduction_id = 12345;
  DataTuple data_tuple{42, 3.14};
  CallbackType callback{99};

  // Create the reduction message
  Message_t message = create_message<DummyAction, DummyComponent>(
      distributed_object_index, reduction_id, data_tuple, callback,
      MessageType::Reduction, false);

  // Get callback address (non-const)
  std::byte* callback_addr = get_callback_address(message);
  CHECK(callback_addr != nullptr);

  // Get callback address (const)
  const std::byte* const_callback_addr =
      get_callback_address(static_cast<const Message_t&>(message));
  CHECK(const_callback_addr != nullptr);

  // Check that the addresses match
  CHECK(callback_addr == const_callback_addr);

  // Get callback object (non-const)
  CallbackType* callback_ptr = get_callback<CallbackType>(message);
  CHECK(callback_ptr != nullptr);
  CHECK(*callback_ptr == callback);

  // Get callback object (const)
  const CallbackType* const_callback_ptr =
      get_callback<CallbackType>(static_cast<const Message_t&>(message));
  CHECK(const_callback_ptr != nullptr);
  CHECK(*const_callback_ptr == callback);

  // Check addresses match.
  CHECK(callback_ptr == reinterpret_cast<CallbackType*>(callback_addr));

  // Check alignment
  CHECK(reinterpret_cast<std::uintptr_t>(callback_addr) % callback_alignment ==
        0);
  CHECK(reinterpret_cast<std::uintptr_t>(const_callback_addr) %
            callback_alignment ==
        0);
}

void test_set_and_get_combine_function_pointer() {
  INFO("Test set_combine_function_pointer and get_combine_function_pointer");

  using namespace findus::reduction;

  // Dummy combine function for testing
  static bool called = false;
  auto dummy_combine = [](Message_t& /*lhs*/, const Message_t& /*rhs*/) {
    called = true;
  };

  // Create a reduction message
  using DataTuple = std::tuple<int, double>;
  using CallbackType = ReductionCallback<DummyAction, DummyComponent>;
  const std::uint32_t distributed_object_index = 7;
  const std::uint64_t reduction_id = 12345;
  DataTuple data_tuple{42, 3.14};
  CallbackType callback{99};
  Message_t message = create_message<DummyAction, DummyComponent>(
      distributed_object_index, reduction_id, data_tuple, callback,
      MessageType::Reduction, false);

  // Set the combine function pointer
  set_combine_function_pointer(message, dummy_combine);

  // Retrieve the function pointer
  auto retrieved_ptr = get_combine_function_pointer(message);

  // Check that the retrieved pointer is not null
  CHECK(retrieved_ptr != nullptr);

  // Call the retrieved function and check that it sets 'called' to true
  called = false;
  retrieved_ptr(message, message);
  CHECK(called == true);

  // Check that the retrieved pointer matches the original function pointer
  // (function pointers to lambdas may not compare equal, so this is optional)
  CHECK(retrieved_ptr == +dummy_combine);
}

void test_contribution_metadata() {
  using findus::detail::get_output;
  CHECK(get_output(Contribution::self_contributed) == "self_contributed");
  CHECK(get_output(Contribution::left_child_contributed) ==
        "left_child_contributed");
  CHECK(get_output(Contribution::right_child_contributed) ==
        "right_child_contributed");
  CHECK(get_output(Contribution::parent_has_local_contributions) ==
        "parent_has_local_contributions");
  CHECK(get_output(Contribution::parents_other_child_has_contributions) ==
        "parents_other_child_has_contributions");
  CHECK(get_output(static_cast<Contribution>(0xff)) == "Unknown");

  // All possible flags
  const std::vector<Contribution> flags = {
      Contribution::self_contributed, Contribution::left_child_contributed,
      Contribution::right_child_contributed,
      Contribution::parent_has_local_contributions,
      Contribution::parents_other_child_has_contributions};

  const std::uint8_t all_contributed_mask =
      static_cast<std::uint8_t>(Contribution::self_contributed) bitor
      static_cast<std::uint8_t>(Contribution::left_child_contributed) bitor
      static_cast<std::uint8_t>(Contribution::right_child_contributed);
  const std::string all_contributed_string =
      std::bitset<8>{all_contributed_mask}.to_string();
  CAPTURE(all_contributed_string);

  // There are 2^5 = 32 possible combinations
  for (std::uint8_t mask = 0; mask < 32; ++mask) {
    // Create a message using the recommended pattern
    const std::uint32_t distributed_object_index = 1;
    const std::uint64_t reduction_id = 42;
    const std::tuple<int> data_tuple{0};
    ReductionCallback<DummyAction, DummyComponent> callback{0};
    Message_t message = create_message<DummyAction, DummyComponent>(
        distributed_object_index, reduction_id, data_tuple, callback,
        MessageType::Reduction, false);

    const std::string mask_string = std::bitset<8>{mask}.to_string();
    CAPTURE(mask_string);

    // Zero metadata before setting flags
    zero_contributed_metadata(message);

    // Set flags according to mask
    for (size_t i = 0; i < flags.size(); ++i) {
      if (mask bitand (0b1 << i)) {
        set_contributed_metadata(message, flags[i]);
      }
    }

    // Check each flag
    for (size_t i = 0; i < flags.size(); ++i) {
      const std::string bits_string =
          std::bitset<8>{
              *reinterpret_cast<std::uint8_t*>(std::next(
                  reinterpret_cast<std::byte*>(message.get_header()),
                  findus::reduction::contributed_metadata_jump_in_bytes))}
              .to_string();
      CAPTURE(bits_string);
      const bool should_be_set = (mask bitand (0b1 << i)) != 0;
      CHECK(get_contributed_metadata(message, flags[i]) == should_be_set);
    }

    CHECK(message_ready(message) ==
          ((all_contributed_mask bitand mask) == all_contributed_mask));

    // Check that no extra bits are set
    CHECK((*reinterpret_cast<std::uint8_t*>(std::next(
               reinterpret_cast<std::byte*>(message.get_header()),
               findus::reduction::contributed_metadata_jump_in_bytes)) bitand
           ~0b11111) == 0);  // Only lower 5 bits should be set

    // Unset flags according to mask
    for (size_t i = 0; i < flags.size(); ++i) {
      if (mask bitand (0b1 << i)) {
        unset_contributed_metadata(message, flags[i]);
      }
    }
    // Check each flag was unset
    for (size_t i = 0; i < flags.size(); ++i) {
      const std::string bits_string =
          std::bitset<8>{
              *reinterpret_cast<std::uint8_t*>(std::next(
                  reinterpret_cast<std::byte*>(message.get_header()),
                  findus::reduction::contributed_metadata_jump_in_bytes))}
              .to_string();
      CAPTURE(bits_string);
      CHECK_FALSE(get_contributed_metadata(message, flags[i]));
    }

    // Check that no extra bits are set
    CHECK((*reinterpret_cast<std::uint8_t*>(std::next(
               reinterpret_cast<std::byte*>(message.get_header()),
               findus::reduction::contributed_metadata_jump_in_bytes)) bitand
           ~0b11111) == 0);  // Only lower 5 bits should be set
  }
}

void test_reduction_contribution_counters() {
  INFO("Test reduction metadata number setters and getters");

  // Create a reduction message
  const std::uint32_t distributed_object_index = 1;
  const std::uint64_t reduction_id = 42;
  const std::tuple<int> data_tuple{0};
  ReductionCallback<DummyAction, DummyComponent> callback{0};
  Message_t message = create_message<DummyAction, DummyComponent>(
      distributed_object_index, reduction_id, data_tuple, callback,
      MessageType::Reduction, false);

  // Test target process id
  const std::int32_t target_pid = 12345;
  set_target_process_id(message, target_pid);
  CHECK(get_target_process_id(message) == target_pid);

  // Test expected number of contributions
  const std::int32_t expected_contributions = 42;
  set_expected_number_of_contributions(message, expected_contributions);
  CHECK(get_expected_number_of_contributions(message) ==
        expected_contributions);

  // Test expected number of root contributions
  const std::int32_t expected_root_contributions = 99;
  set_expected_number_of_root_contributions(message,
                                            expected_root_contributions);
  CHECK(get_expected_number_of_root_contributions(message) ==
        expected_root_contributions);

  // Now set all three to different values and check for collisions
  const std::int32_t val1 = 111;
  const std::int32_t val2 = 222;
  const std::int32_t val3 = 333;
  set_target_process_id(message, val1);
  set_expected_number_of_contributions(message, val2);
  set_expected_number_of_root_contributions(message, val3);

  CHECK(get_target_process_id(message) == val1);
  CHECK(get_expected_number_of_contributions(message) == val2);
  CHECK(get_expected_number_of_root_contributions(message) == val3);
}
}  // namespace
}  // namespace reduction
}  // namespace findus

TEST_CASE("Message") {
  findus::test_create_message_alignment_and_values();
  findus::test_create_message_serialized();
  findus::test_create_broadcast_to_message();
  findus::test_create_message();
  findus::test_copy_message();

  findus::reduction::test_set_and_get_id();
  findus::reduction::test_set_and_get_data_offset();
  findus::reduction::test_set_and_get_callback_offset();
  findus::reduction::test_create_message();
  findus::reduction::test_get_callback_address_and_get_callback();
  findus::reduction::test_set_and_get_combine_function_pointer();
  findus::reduction::test_contribution_metadata();
  findus::reduction::test_reduction_contribution_counters();
}

#endif
