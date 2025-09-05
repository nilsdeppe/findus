// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <iosfwd>
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
 * \param number_of_elements_on_pid The number of collection elements on the
 *        process.
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
 * \param message_type The type of message (e.g., Invoke, Broadcast, etc.).
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

namespace reduction {
/*!
 * \brief The size in bytes of the metadata block following the MessageHeader in
 * a reduction message.
 *
 * This block is reserved for future use and is currently zero-initialized.
 */
constexpr std::size_t metadata_block_size = 64;

/*!
 * \brief The alignment in bytes for the reduction callback object in a
 * reduction message.
 *
 * The callback is aligned to 64 bytes to avoid false sharing and to ensure
 * proper alignment for performance on modern hardware.
 */
constexpr std::size_t callback_alignment = 64;

/*!
 * \brief Sets the reduction ID in the data portion of a reduction message.
 *
 * The reduction ID is stored at the beginning of the data buffer in the
 * message. This uniquely identifies the reduction operation.
 *
 * \param message The message in which to set the reduction ID.
 * \param reduction_id The unique 64-bit reduction ID to set.
 */
void set_id(Message_t& message, std::uint64_t reduction_id);

/*!
 * \brief Retrieves the reduction ID from the data portion of a reduction
 * message.
 *
 * The reduction ID is read from the beginning of the data buffer in the
 * message.
 *
 * \param message The message from which to retrieve the reduction ID.
 * \return The 64-bit reduction ID stored in the message.
 */
std::uint64_t get_id(const Message_t& message);

/*!
 * \brief Sets the data offset in the data portion of a reduction message.
 *
 * The data offset indicates the byte offset to the reduction data within
 * the message buffer.
 *
 * \param message The message in which to set the data offset.
 * \param data_offset The 32-bit offset to the reduction data.
 */
void set_data_offset(Message_t& message, std::uint32_t data_offset);

/*!
 * \brief Retrieves the data offset from the data portion of a reduction
 * message.
 *
 * The data offset indicates the byte offset to the reduction data within
 * the message buffer.
 *
 * \param message The message from which to retrieve the data offset.
 * \return The 32-bit data offset stored in the message.
 */
std::uint32_t get_data_offset(const Message_t& message);

/// @{
/*!
 * \brief Returns a pointer to the reduction data in a reduction message.
 *
 * This function computes and returns a pointer to the start of the reduction
 * data within the message buffer. The pointer can be used to access or
 * manipulate the reduction data directly.
 *
 * \param message The reduction message containing the data.
 * \return Pointer to the start of the reduction data.
 */
std::byte* get_data_pointer(Message_t& message);

const std::byte* get_data_pointer(const Message_t& message);
/// @}

/*!
 * \brief Sets the size of the reduction data in a reduction message.
 *
 * This function stores the size (in bytes) of the reduction data within the
 * message metadata. This information is used to correctly interpret and
 * extract the reduction data from the message buffer.
 *
 * \param message The reduction message in which to set the data size.
 * \param data_size The size of the reduction data in bytes.
 */
void set_data_size(Message_t& message, std::uint32_t data_size);

/*!
 * \brief Retrieves the size of the reduction data from a reduction message.
 *
 * This function reads the size (in bytes) of the reduction data from the
 * message metadata. This allows code to determine how much data is stored
 * in the reduction message.
 *
 * \param message The reduction message from which to retrieve the data size.
 * \return The size of the reduction data in bytes.
 */
std::uint32_t get_data_size(Message_t& message);

/*!
 * \brief Sets the callback offset in the data portion of a reduction message.
 *
 * The callback offset indicates the byte offset to the post-reduction
 * callback within the message buffer.
 *
 * \param message The message in which to set the callback offset.
 * \param callback_offset The 32-bit offset to the callback.
 */
void set_callback_offset(Message_t& message, std::uint32_t callback_offset);

/*!
 * \brief Retrieves the callback offset from the data portion of a reduction
 * message.
 *
 * The callback offset indicates the byte offset to the post-reduction
 * callback within the message buffer.
 *
 * \param message The message from which to retrieve the callback offset.
 * \return The 32-bit callback offset stored in the message.
 */
std::uint32_t get_callback_offset(const Message_t& message);

/// @{
/*!
 * \brief Returns a pointer to the callback object in a reduction message.
 *
 * This function computes the address of the callback object within the message
 * buffer using the callback offset stored in the reduction message metadata.
 *
 * \param message The reduction message.
 * \return Pointer to the start of the callback object.
 */
std::byte* get_callback_address(Message_t& message);
const std::byte* get_callback_address(const Message_t& message);
/// @}

/// @{
/*!
 * \brief Returns a typed pointer to the callback object in a reduction message.
 *
 * This function casts the callback address to the specified callback type.
 *
 * \tparam CallbackType The type of the callback object.
 * \param message The reduction message.
 * \return Pointer to the callback object of type CallbackType.
 */
template <class CallbackType>
CallbackType* get_callback(Message_t& message) {
  return reinterpret_cast<CallbackType*>(get_callback_address(message));
}
template <class CallbackType>
const CallbackType* get_callback(const Message_t& message) {
  return reinterpret_cast<const CallbackType*>(get_callback_address(message));
}
/// @}

/*!
 * \brief Sets the function pointer for combining reduction messages.
 *
 * Stores a pointer to a function that combines two Message_t objects
 * in the message metadata. This function is used to perform the reduction
 * operation when combining messages.
 *
 * \param message The message in which to store the function pointer.
 * \param pointer The function pointer to store. Must have signature:
 *        `void(Message_t&, const Message_t&)`.
 */
void set_combine_function_pointer(Message_t& message,
                                  void (*pointer)(Message_t&,
                                                  const Message_t&));

/*!
 * \brief Retrieves the function pointer for combining reduction messages.
 *
 * Returns the function pointer stored in the message metadata that can be
 * used to combine two Message_t objects for reduction.
 *
 * \param message The message from which to retrieve the function pointer.
 * \return Function pointer with signature: `void(Message_t&, const Message_t&)`
 */
auto get_combine_function_pointer(const Message_t& message)
    -> void (*)(Message_t&, const Message_t&);

/*!
 * \brief Enum representing contribution metadata for reduction messages.
 *
 * The Contribution enum encodes the state of contributions in a reduction
 * operation. Each value is a bit flag that can be combined to represent
 * multiple states in the metadata block of a reduction message.
 */
enum class Contribution : std::uint8_t {
  /// The current process has contributed to the reduction.
  self_contributed = 0b1,
  /// The left child process has contributed to the reduction.
  left_child_contributed = 0b10,
  /// The right child process has contributed to the reduction.
  right_child_contributed = 0b100,
  /// The parent process has local contributions.
  parent_has_local_contributions = 0b1000,
  /// The parent's other child will contribute to the reduction.
  parents_other_child_has_contributions = 0b10000
};

/// \brief Stream operator for `Contribution`.
std::ostream& operator<<(std::ostream& os, Contribution contribution);

/// \brief Array of the self and child contributed flags used to check if a
/// message has all necessary contributed data.
constexpr static std::array<Contribution, 3> core_contributed{
    Contribution::self_contributed, Contribution::left_child_contributed,
    Contribution::right_child_contributed};

/// \brief Returns `true` if all of the `core_contributed` flags are set in the
/// message.
bool message_ready(const Message_t& message);

/*!
 * \brief Zeros out the contribution metadata in a reduction message.
 *
 * This function resets the contribution metadata block in the message to zero,
 * clearing all contribution flags.
 *
 * \param message The reduction message whose metadata will be zeroed.
 */
void zero_contributed_metadata(Message_t& message);

/*!
 * \brief Sets a specific contribution flag in the reduction message metadata.
 *
 * This function sets the specified `Contribution` flag in the message's
 * metadata block, marking the corresponding contribution state as active.
 *
 * \param message The reduction message to update.
 * \param contribution The `Contribution` flag to set.
 */
void set_contributed_metadata(Message_t& message, Contribution contribution);

/*!
 * \brief Unsets a specific contribution flag in the reduction message metadata.
 *
 * This function unsets the specified `Contribution` flag in the message's
 * metadata block, marking the corresponding contribution state as inactive.
 *
 * \param message The reduction message to update.
 * \param contribution The `Contribution` flag to unset.
 */
void unset_contributed_metadata(Message_t& message, Contribution contribution);

/*!
 * \brief Returns true if the specified `Contribution` flag is set in the
 * message.
 *
 * \param message The reduction message to query.
 * \param contribution The `Contribution` flag to check.
 * \return `true` if the flag is set, `false` otherwise.
 */
bool get_contributed_metadata(const Message_t& message,
                              Contribution contribution);

/*!
 * \brief Creates a reduction message with metadata, data, and callback.
 *
 * This function allocates and constructs a reduction message buffer containing:
 *   - A MessageHeader at the start.
 *   - A 64-byte metadata block after the header (zero-initialized).
 *   - The reduction data (as a std::tuple of arguments), properly aligned.
 *   - The reduction callback object, aligned to 64 bytes.
 *
 * The memory layout of the resulting message buffer is:
 * ```
 *   [MessageHeader][64-byte metadata][data][callback (64-byte aligned)]
 * ```
 * The metadata sent along is
 * 1. The reduction id (`std::uint64_t`, 8 bytes). Retrieve using
 *    `rts::reduction::get_id()`.
 * 2. The offset of the data relative to the MessageHeader address
 *    (`std::uint32_t`, 4 bytes).Retrieve using
 *    `rts::reduction::get_data_offset()`.
 * 3. The offset of the callback relative to the MessageHeader address
 *    (`std::uint32_t`, 4 bytes). Retrieve using
 *    `rts::reduction::get_callback_offset()`.
 *
 * The function computes the correct offsets and alignments for the data and
 * callback, constructs them in-place, and sets the appropriate metadata fields
 * in the message. The resulting buffer is wrapped in a Message_t, which takes
 * ownership of the memory.
 *
 * \tparam Args
 *   The types of the arguments to be stored in the data tuple.
 * \tparam BroadcastAction
 *   The action type for the broadcast.
 * \tparam BroadcastParallelComponent
 *   The parallel component type for the broadcast.
 * \tparam ReductionCallback
 *   The template for the reduction callback type.
 *
 * \param distributed_object_index
 *   The distributed object index for the message header.
 * \param reduction_id
 *   The unique 64-bit reduction ID for this reduction operation.
 * \param args
 *   The arguments to be stored in the data tuple (as a std::tuple).
 * \param callback
 *   The reduction callback object to be invoked after reduction.
 * \param message_type
 *   The type of message (default is MessageType::Reduction).
 *
 * \return
 *   A Message_t containing the allocated buffer with header, metadata, data,
 *   and callback.
 *
 * \note
 *   - The buffer is aligned to ensure both the data tuple and callback are
 *     properly aligned.
 *   - The metadata block is reserved for future use and is zero-initialized.
 *   - The function sets the reduction ID, data offset, and callback offset
 *     in the message metadata.
 */
template <class BroadcastAction, class BroadcastParallelComponent,
          template <class...> class ReductionCallback, class... Args>
Message_t create_message(
    const std::uint32_t distributed_object_index,
    const std::uint64_t reduction_id, std::tuple<Args...> args,
    ReductionCallback<BroadcastAction, BroadcastParallelComponent,
                      std::decay_t<Args>...>
        callback,
    const MessageType message_type) {
  using DataTuple = std::tuple<Args...>;
  using CallbackType =
      ReductionCallback<BroadcastAction, BroadcastParallelComponent,
                        std::decay_t<Args>...>;

  constexpr std::size_t header_size = sizeof(MessageHeader);
  constexpr std::size_t data_align = alignof(DataTuple);
  constexpr std::size_t callback_align = callback_alignment;

  // Compute data_offset: header + metadata, aligned for DataTuple
  const std::size_t unaligned_data_offset = header_size + metadata_block_size;
  const std::size_t data_offset =
      unaligned_data_offset +
      ((data_align - (unaligned_data_offset % data_align)) % data_align);

  // Compute callback_offset: after data, aligned to 64 bytes
  const std::size_t unaligned_callback_offset = data_offset + sizeof(DataTuple);
  const std::size_t callback_offset =
      unaligned_callback_offset +
      ((callback_align - (unaligned_callback_offset % callback_align)) %
       callback_align);

  const std::size_t total_size = callback_offset + sizeof(CallbackType);

  // Allocate buffer
  std::unique_ptr<std::byte[]> buffer(new (std::align_val_t(std::max(
      std::max(alignof(MessageHeader), data_align), callback_alignment)))
                                          std::byte[total_size]);

  // Placement-new the header
  MessageHeader* header = new (buffer.get()) MessageHeader(
      {0, 0}, MessageHeader::reduction_message_collection_index(), total_size,
      distributed_object_index, data_offset, -1, -1,
      std::numeric_limits<std::uint64_t>::max(), false, message_type);
  header->set_data_alignment(alignof(DataTuple));

  // Zero the metadata block for safety/future use
  std::memset(buffer.get() + header_size, 0, metadata_block_size);

  // Construct the data and callback
  new (buffer.get() + data_offset) DataTuple(std::move(args));
  new (buffer.get() + callback_offset) CallbackType(std::move(callback));

  // Wrap in Message_t
  Message_t message{std::move(buffer)};

  // Set metadata using reduction helpers
  set_id(message, reduction_id);
  set_data_offset(message, static_cast<std::uint32_t>(data_offset));
  set_callback_offset(message, static_cast<std::uint32_t>(callback_offset));

  return message;
}
}  // namespace reduction
}  // namespace rts
