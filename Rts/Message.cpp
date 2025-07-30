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

TEST_CASE("Message") { rts::test_copy_message(); }

#endif
