// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstdint>
#include <iosfwd>
#include <limits>
#include <stdexcept>
#include <string>

#include "Rts/Detail/MemberFunctionPtr.hpp"
#include "Rts/Exceptions/Exception.hpp"

namespace rts {
/*!
 * \brief The type of message being sent.
 *
 * The message types can be up to 3 bits in size allowing for 8 different
 * message types.
 */
enum class MessageType : std::uint8_t {
  /// \brief Used to check if this enum was not initialized.
  Uninitialized = 0b000,
  /// \brief A point-to-point threaded action invocation.
  Invoke = 0b001,
  /// \brief A broadcast to all elements of a collection.
  Broadcast = 0b010,
  /// \brief A broadcast to a subset of elements of a collection.
  BroadcastTo = 0b011,
  /// \brief A reduction over all elements of a collection.
  Reduction = 0b100,
  /// \brief A reduction over a subset of elements of a collection.
  ReductionOver = 0b101
};

/// \brief Stream operator for `rts::MessageType`.
std::ostream& operator<<(std::ostream& os, MessageType t);

/*!
 * \brief The header of every message sent between nodes. This is used to
 * identify which distributed object will have the function invoked on it.
 *
 * The class members represent the following:
 * - `member_function_ptr` the 16 byte member function pointer. In practice
 *   this is a _relative_ pointer since pointer address cannot be safely sent
 *   across address space boundaries, but relative addresses can be. A typical
 *   use case would be to compute the address relative to some `T::anchor()`
 *   member function, e.g. `member_function_ptr = function - anchor`. On the
 *   receiving end the new `function` address can be computed by adding the
 *   local (receiving-end) `anchor` value.
 * - `target_collection_index` is the 8-byte index of the collection element
 *   that will receive the message.
 * - `number_of_bytes_in_message` stores both whether the data was serialized
 *   (sent across an address space boundary) and also the size of the
 *   message. These pieces of information can be separately accessed using the
 *   functions `number_of_bytes_in_message()` and `data_was_serialized()`. The
 *   function `set_data_was_serialized()` must be used to set the flag for
 *   that data was serialized. Not setting the flag is undefined behavior with
 *   no method of diagnosing it.
 *   Note: the `number_of_bytes_in_message` is the total bytes in the message,
 *   counting both the `MessageHeader` and the serialized data.
 * - `distributed_object_index` is the index of the distributed object (i.e.,
 *   a class type). While it could be any desired index type, hashing a
 *   human-readable name of the distributed object class is recommended.
 * - `data_offset` is the number of bytes from the message's address to the
 *   byte stream of data, which may or may not have been serialized. The
 *   functions `rts::create_data_in_message()` and `rts::data_from_message()`
 *   should be used to access the data rather than doing the calculations
 *   directly.
 * - `source_process_id` is the index of the process ID (e.g. MPI rank) that is
 *   sending the message.
 * - `destination_process_id` is the index of the process ID that is receiving
 *   the message.
 *
 * See
 * - `rts::create_data_in_message()`
 * - `rts::data_from_message()`
 */
struct alignas(64) MessageHeader {
 public:
  MessageHeader(detail::MemberFunctionPtr member_function_ptr,
                std::uint64_t target_collection_index,
                std::uint64_t number_of_bytes_in_message,
                std::uint32_t distributed_object_index,
                std::uint32_t data_offset, std::int32_t source_process_id,
                std::int32_t destination_process_id,
                std::uint64_t quiescence_detection_sweep_number,
                bool was_serialized, MessageType message_type);

  /// \brief The value of `target_collection_index` used when the distributed
  /// object is not a collection.
  static constexpr std::uint64_t no_collection_index() {
    return std::numeric_limits<std::uint64_t>::max();
  }

  /// \brief The value of `target_collection_index` used for reduction
  /// messages since those are essentially sent "to the collection".
  static constexpr std::uint64_t reduction_message_collection_index() {
    return std::numeric_limits<std::uint64_t>::max() - 1;
  }

  /// \brief Get the member function pointer.
  const detail::MemberFunctionPtr& member_function_ptr() const {
    return member_function_ptr_;
  }
  /// \brief The index for the type of parallel component/distributed object.
  std::uint32_t distributed_object_index() const {
    return distributed_object_index_;
  }
  /// \brief The index into the parallel component collection
  std::uint64_t target_collection_index() const {
    return target_collection_index_;
  }
  /*!
   * \brief Get the number of bytes in the message.
   *
   * Note: this is the total bytes in the message, counting the
   * `MessageHeader`, padding, and the serialized data.
   */
  std::uint64_t number_of_bytes_in_message() const {
    return number_of_bytes_in_message_mask bitand number_of_bytes_in_message_;
  }

  /*!
   * \brief Retrieves the type of message, `MessageType`.
   */
  MessageType message_type() const {
    return static_cast<MessageType>(
        (message_type_mask bitand number_of_bytes_in_message_) >> 60);
  }
  /// \brief Returns `true` if the message is a broadcast.
  bool is_broadcast() const { return message_type() == MessageType::Broadcast; }
  /// \brief Returns `true` if the message is a broadcast to a subset of a
  /// collection.
  bool is_broadcast_to() const {
    return message_type() == MessageType::BroadcastTo;
  }

  /// \brief Returns `true` if the data was serialized and `false` if the data
  /// was in-place constructed.
  bool data_was_serialized() const {
    return static_cast<bool>(data_was_serialized_mask bitand
                             number_of_bytes_in_message_);
  }

  /// @{
  /*!
   * \brief Returns the address of the data/byte stream in a message.
   */
  char* data_location() { return reinterpret_cast<char*>(this) + data_offset_; }
  const char* data_location() const {
    return reinterpret_cast<const char*>(this) + data_offset_;
  }
  /// @}

  /// \brief Get the process ID of the message sender/source.
  std::int32_t source_process_id() const { return source_process_id_; }

  /// \brief Get the process ID of the message receiver/destination/target.
  std::int32_t destination_process_id() const {
    return destination_process_id_;
  }

  /// \brief Get the global quiescence detection sweep number.
  std::uint64_t quiescence_detection_sweep_number() const {
    return quiescence_detection_sweep_number_;
  }

  std::uint32_t data_alignment() const {
    return (data_alignment_mask bitand number_of_bytes_in_message_) >> 52;
  }

  std::uint32_t data_offset() const { return data_offset_; }

  /// \brief Update the `data_offset`.
  void data_offset(const std::uint32_t data_offset) {
    data_offset_ = data_offset;
  }

  /// \brief Changes the destination to process ID.
  ///
  /// This is used in broadcast operations where a broadcast is sent to each
  /// child process. We copy the original message and then update the
  /// destination, which is original set to the self process ID.
  void change_destination_process_id(std::int32_t destination_process_id);

  /// \brief Convert Broadcast or BroadcastTo message to an Invoke message.
  ///
  /// This is used in broadcast operations where the broadcast message has a
  /// placeholder target collection index that is overridden to the specified
  /// target index that is node-local and the rts::MessageType is changed from
  /// rts::MessageType::Broadcast or rts::MessageType::BroadcastTo to
  /// rts::MessageType::Invoke. An Exception is thrown if the message type
  /// isn't for a broadcast.
  void convert_broadcast_to_invoke(std::uint64_t target_collection_index);

  /// \brief The number of bits used to store type alignment information.
  static constexpr std::uint8_t alignment_bits = 8;
  /// \brief The maximum type alignment supported.
  static constexpr std::uint64_t max_alignment =
      (std::uint64_t(1) << alignment_bits) - 1;

  /// \brief The bitmask used to retrieve whether the data was serialized.
  static constexpr std::uint64_t data_was_serialized_mask = std::uint64_t{0b1}
                                                            << 63;
  /// \brief The bitmask used to retrieve the rts::MessageType.
  static constexpr std::uint64_t message_type_mask = std::uint64_t{0b111} << 60;
  /// \brief The bitmask used to retrieve the data alignment.
  static constexpr std::uint64_t data_alignment_mask =
      MessageHeader::max_alignment << 52;
  /// \brief The bitmask used to retrieve the number of bytes in the message.
  static constexpr std::uint64_t number_of_bytes_in_message_mask =
      (std::uint64_t{1} << 40) - 1;

  /*!
   * \brief Sets the data alignment information in the message header.
   *
   * \param alignment The alignment (in bytes) to be stored in the message
   * header.
   *
   * \note The alignment value must fit within the number of bits reserved for
   * alignment (8 bits).
   */
  void set_data_alignment(const std::uint64_t alignment) {
    // Zero out bits.
    number_of_bytes_in_message_ =
        (compl data_alignment_mask) bitand number_of_bytes_in_message_;
    // Set alignment.
    number_of_bytes_in_message_ =
        (alignment << 52) bitor number_of_bytes_in_message_;
  }

 private:
  template <class DataTypeReturned>
  friend auto create_data_in_message(MessageHeader& message_header)
      -> DataTypeReturned*;

  detail::MemberFunctionPtr member_function_ptr_ = {};
  std::uint64_t target_collection_index_ = 0;
  // Holds several different pieces of information:
  // 1. Lowest 40 bits are the number of bytes in the message.
  //    Note: 40 bits gives a little over 1.09TB per message.
  // 2. Highest bit is a flag as to whether the data was serialized.
  // 3. 2nd to 4th (inclusive) highest bits (3 total) are the message type.
  // 4. 5th to 12th (inclusive) highest bits are the alignment of the data.
  //    Note: this means data may be at most 255 byte aligned.
  // 5. 13th to 24th (inclusive) are currently unused.
  std::uint64_t number_of_bytes_in_message_ = 0;
  std::uint32_t distributed_object_index_ = 0;
  std::uint32_t data_offset_ = 0;
  std::int32_t source_process_id_ = -1;
  std::int32_t destination_process_id_ = -1;
  std::uint64_t quiescence_detection_sweep_number_ =
      std::numeric_limits<std::uint64_t>::max();
};

/// \brief Equivalence operator for rts::MessageHeader
bool operator==(const MessageHeader& lhs, const MessageHeader& rhs);

/// \brief Inequivalence operator for rts::MessageHeader
bool operator!=(const MessageHeader& lhs, const MessageHeader& rhs);

/// \brief Stream operator for rts::MessageHeader
std::ostream& operator<<(std::ostream& os, const MessageHeader& header);

/*!
 * \brief Creates the `DataTypeReturned` in the data portion of the message.
 *
 * This is used for intranode data where we can in-place construct a
 * `std::tuple` of the arguments in the message and then move the arguments
 * into the tuple. It can also be used for internode messages where all the
 * types are trivially copyable under the assumption that trivially copyable
 * means the type can be copied without having to worry about pointers
 * becoming in correct to heap data.
 *
 * \tparam DataTypeReturned The type of the data created in the message.
 * \param message_header The message in which to create the data.
 */
template <class DataTypeReturned>
auto create_data_in_message(MessageHeader& message_header)
    -> DataTypeReturned* {
  if ((reinterpret_cast<std::uintptr_t>(message_header.data_location()) %
       alignof(DataTypeReturned)) != 0) {
    throw Exception{
        "Unable to convert data at address to the requested type because the "
        "alignment of the requested type is stricter than the alignment of the "
        "data. This means it is not possible to do a safe conversion or to "
        "have the data be interpreted as the requested type. The alignment of "
        "the type is " +
        std::to_string(alignof(DataTypeReturned)) +
        std::string{" and the memory address is " +
                    std::to_string(reinterpret_cast<std::uintptr_t>(
                        message_header.data_location()))}};
  }
  message_header.set_data_alignment(alignof(DataTypeReturned));
  DataTypeReturned* data =
      new (message_header.data_location()) DataTypeReturned{};
  return data;
}

/// @{
/*!
 * \brief Converts the internal data pointer from the message header to the
 * desired type.
 *
 * This does not do any deserialization, it just does a `reinterpret_cast` of
 * the pointer. It is up to the user to make sure the conversion is safe.
 *
 * We verify that the alignment of the type and the alignment of the data
 * agree, and if not throw a `std::runtime_error`.
 *
 * \tparam DataTypeReturned The type that the data in the message will be
 * interpreted as.
 * \param message_header The message from which to get the data.
 */
template <class DataTypeReturned>
auto data_from_message(MessageHeader& message_header) -> DataTypeReturned* {
  if ((reinterpret_cast<std::uintptr_t>(message_header.data_location()) %
       alignof(DataTypeReturned)) != 0) {
    throw Exception{
        "Unable to convert data at address to the requested type because the "
        "alignment of the requested type is stricter than the alignment of the "
        "data. This means it is not possible to do a safe conversion or to "
        "have the data be interpreted as the requested type. The alignment of "
        "the type is " +
        std::to_string(alignof(DataTypeReturned)) +
        std::string{" and the memory address is " +
                    std::to_string(reinterpret_cast<std::uintptr_t>(
                        message_header.data_location()))}};
  }
  if (alignof(DataTypeReturned) != message_header.data_alignment()) {
    throw Exception{"The data alignment in the message (" +
                    std::to_string(message_header.data_alignment()) +
                    ") does not match the alignment of the returned type " +
                    std::to_string(alignof(DataTypeReturned))};
  }
  return reinterpret_cast<DataTypeReturned*>(message_header.data_location());
}

template <class DataTypeReturned>
auto data_from_message(const MessageHeader& message_header)
    -> const DataTypeReturned* {
  return data_from_message<DataTypeReturned>(
      const_cast<MessageHeader&>(message_header));
}
/// @}
}  // namespace rts
