// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstddef>
#include <cstring>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "Rts/MessageHeader.hpp"

namespace rts {
/// \cond
class DistributedTaskDriver;
template <class MessageType, class ProcessLocalDataType>
class ThreadPool;
/// \endcond

/*!
 * \brief The type of the messages sent by the runtime system.
 *
 * The underlying data is essentially just a byte stream, which is stored in a
 * `std::unique_ptr<std::byte[]>`. There is currently no small message
 * optimization.
 */
struct Message_t {
  std::unique_ptr<std::byte[]> message{nullptr};

  /// @{
  /// \brief Returns the message header.
  MessageHeader* get_header() {
    return reinterpret_cast<MessageHeader*>(message.get());
  }
  const MessageHeader* get_header() const {
    return reinterpret_cast<MessageHeader*>(message.get());
  }
  /// @}

  /// \brief Executes the message.
  static bool execute(
      rts::ThreadPool<Message_t, rts::DistributedTaskDriver*>& /*pool*/,
      const std::uint32_t thread_id, Message_t& message,
      DistributedTaskDriver* distributed_task_driver);
};

/// \brief Make a copy of Message_t.
Message_t copy(const Message_t& message);

/*!
 * \brief Allocates and constructs a Message_t with a MessageHeader and data
 * tuple.
 *
 * This function allocates a buffer using the provided allocator, constructs a
 * MessageHeader at the start of the buffer using placement new, and then
 * constructs a std::tuple of the provided arguments at the correct offset
 * (with proper alignment) after the header. The resulting buffer is wrapped
 * in a Message_t, which takes ownership of the memory.
 *
 * \tparam Args
 *   The types of the arguments to be stored in the data tuple.
 * \param member_function_ptr
 *   The member function pointer to be stored in the MessageHeader.
 * \param target_collection_index
 *   The collection index for the target (or
 *   MessageHeader::no_collection_index() for non-collections).
 * \param distributed_object_index
 *   The distributed object index for the header.
 * \param source_process_id
 *   The process ID of the sender.
 * \param destination_process_id
 *   The process ID of the receiver.
 * \param quiescence_detection_sweep_number
 *   The sweep number for quiescence detection.
 * \param was_serialized
 *   Whether the data was serialized (true) or in-place constructed (false).
 * \param message_type
 *   The type of message (e.g., Invoke, Broadcast, etc.).
 * \param args
 *   The arguments to move into the data tuple (as a std::tuple).
 * \return
 *   A Message_t containing the allocated buffer with header and data.
 *
 * \note
 *   - The buffer is aligned to ensure the data tuple is properly aligned after
 *     the header.
 *   - The function takes ownership of the allocated buffer via Message_t.
 *   - The data tuple is move-constructed into the buffer.
 */
template <class... Args>
Message_t create_message(const detail::MemberFunctionPtr member_function_ptr,
                         const std::uint64_t target_collection_index,
                         const std::uint32_t distributed_object_index,
                         const std::int32_t source_process_id,
                         const std::int32_t destination_process_id,
                         const std::uint64_t quiescence_detection_sweep_number,
                         const bool was_serialized,
                         const MessageType message_type,
                         std::tuple<Args...> args) {
  using Data_t = std::tuple<std::decay_t<Args>...>;
  static_assert(std::is_same_v<Data_t, std::tuple<Args...>>);
  constexpr std::size_t data_alignment = alignof(Data_t);

  // Compute offset for data to ensure correct alignment after header.
  constexpr std::size_t header_size = sizeof(rts::MessageHeader);
  const std::size_t data_offset =
      header_size
      // Add extra bytes to make sure we can align Data_t
      // properly. We compute the remainder of the MessageHeader size and
      // the alignment of the data. This would give us, e.g. 5 bytes, which
      // means we have e.g. 37 bytes for MessageHeader. The amount we
      // would need to align then is given by the C++:
      + (data_alignment - header_size % data_alignment);

  // Total buffer size: header + padding + data
  const std::size_t number_of_bytes_in_message = data_offset + sizeof(Data_t);

  std::unique_ptr<std::byte[]> buffer(
      new (std::align_val_t(std::max(alignof(MessageHeader), alignof(Data_t))))
          std::byte[number_of_bytes_in_message]);

  // Placement-new copy-construct the header at the start of the buffer
  auto* header_ptr = new (buffer.get()) rts::MessageHeader(
      member_function_ptr, target_collection_index, number_of_bytes_in_message,
      distributed_object_index, data_offset, source_process_id,
      destination_process_id, quiescence_detection_sweep_number, was_serialized,
      message_type);
  *rts::create_data_in_message<Data_t>(*header_ptr) = std::move(args);

  return {std::move(buffer)};
}

/*!
 * \brief Allocates and constructs a BroadcastTo Message_t.
 *
 * Allocates a buffer using the provided allocator, placement-news the given
 * MessageHeader at the start, writes extra metadata (target pid, number of
 * elements, collection indices), and then copies the data tuple from a
 * provided buffer into the correct location.
 *
 * \param member_function_ptr The member function pointer for the header.
 * \param distributed_object_index The distributed object index for the header.
 * \param source_process_id The source process ID for the header.
 * \param destination_process_id The destination process ID for the header.
 * \param quiescence_detection_sweep_number The sweep number for the header.
 * \param was_serialized Whether the data was serialized.
 * \param data_alignment The alignment of the underlying data.
 * \param data_size The size of the underlying data.
 * \param data_ptr Pointer to the data tuple to copy into the message.
 * \return Message_t containing the allocated buffer with header, metadata, and
 * data.
 */
Message_t create_broadcast_to_message(
    const rts::detail::MemberFunctionPtr& member_function_ptr,
    std::uint32_t distributed_object_index, std::int32_t source_process_id,
    std::int32_t destination_process_id,
    std::uint64_t quiescence_detection_sweep_number, bool was_serialized,
    int number_of_elements_on_pid, std::uint64_t data_alignment,
    std::uint64_t data_size, const void* data_ptr);

/*!
 * \brief Allocates and constructs a Message_t for a local collection element.
 *
 * Allocates a buffer, constructs a MessageHeader at the start, and copies
 * the provided data into the buffer at the correct offset and alignment.
 * Used to create a message that will invoke an action on a specific element
 * of a collection parallel component on the local process.
 *
 * \param member_function_ptr The member function pointer for the action.
 * \param collection_index The collection index for the target element.
 * \param distributed_object_index The distributed object index for the header.
 * \param source_process_id The process ID of the sender.
 * \param destination_process_id The process ID of the receiver.
 * \param quiescence_detection_sweep_number The sweep number for QD.
 * \param was_serialized True if the data was serialized, false if in-place.
 * \param data_alignment The alignment of the data to be copied.
 * \param data_size The size of the data to be copied.
 * \param data_ptr Pointer to the data to copy into the message.
 * \return Message_t containing the allocated buffer with header and data.
 */
Message_t create_local_invoke_message(
    const rts::detail::MemberFunctionPtr& member_function_ptr,
    std::uint64_t collection_index, std::uint32_t distributed_object_index,
    std::int32_t source_process_id, std::int32_t destination_process_id,
    std::uint64_t quiescence_detection_sweep_number, bool was_serialized,
    std::uint64_t data_alignment, std::uint64_t data_size,
    const void* data_ptr);
}  // namespace rts
