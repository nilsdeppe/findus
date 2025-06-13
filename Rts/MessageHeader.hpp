// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include "Rts/Detail/MemberFunctionPtr.hpp"

namespace rts {
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
 * - `source_rank` is the index of the MPI rank that is sending the message.
 * - `destination_rank` is the index of the MPI that is receiving the message.
 *
 * See
 * - `rts::number_of_bytes_in_message()`
 * - `rts::data_was_serialized()`
 * - `rts::set_data_was_serialized()`
 * - `rts::data_from_message()`
 * - `rts::MessageHeader::no_collection_index()`
 * - `rts::data_location()`
 * - `rts::create_data_in_message()`
 * - `rts::data_from_message()`
 */
struct alignas(64) MessageHeader {
  detail::MemberFunctionPtr member_function_ptr = {};
  std::uint64_t target_collection_index = 0;
  std::uint64_t number_of_bytes_in_message = 0;
  std::uint32_t distributed_object_index = 0;
  std::uint32_t data_offset = 0;
  std::int32_t source_rank = -1;
  std::int32_t destination_rank = -1;
  std::uint64_t quiescence_detection_sweep_number =
      std::numeric_limits<std::uint64_t>::max();

  /// \brief The value of `target_collection_index` used when the distributed
  /// object is not a collection.
  static constexpr std::uint64_t no_collection_index() {
    return std::numeric_limits<std::uint64_t>::max();
  }
};

/*!
 * \brief Get the number of bytes in the message.
 *
 * Note: this is the total bytes in the message, counting the
 * `MessageHeader`, padding, and the serialized data.
 */
inline std::uint64_t number_of_bytes_in_message(const MessageHeader& message) {
  return std::uint64_t{std::numeric_limits<std::uint64_t>::max() >> 1} bitand
         message.number_of_bytes_in_message;
}

/// \brief Returns `true` if the data was serialized and `false` if the data
/// was in-place constructed.
inline bool data_was_serialized(const MessageHeader& message) {
  return static_cast<bool>((std::uint64_t{0b1} << 63) bitand
                           message.number_of_bytes_in_message);
}

/// \brief Set to `true` if the data was serialized and `false` if the data
/// was in-place constructed.
inline void set_data_was_serialized(MessageHeader& message,
                                    const bool data_was_serialized) {
  if (data_was_serialized) {
    // Set highest bit to 1
    message.number_of_bytes_in_message =
        std::uint64_t{0b1} << 63 bitor message.number_of_bytes_in_message;
  } else {
    // Set highest bit to zero
    message.number_of_bytes_in_message =
        std::uint64_t{std::numeric_limits<std::uint64_t>::max() >> 1} bitand
        message.number_of_bytes_in_message;
  }
}

/*!
 * \brief Returns the address of the data/byte stream in a message.
 */
inline char* data_location(MessageHeader& message_header) {
  return reinterpret_cast<char*>(&message_header) + message_header.data_offset;
}

/*!
 * \brief Creates the `DataTypeReturned` in the data portion of the message.
 *
 * This is used for intranode data where we can in-place construct a
 * `std::tuple` of the arguments in the message and then move the arguments
 * into the tuple. It can also be used for internode messages where all the
 * types are trivially copyable under the assumption that trivially copyable
 * means the type can be copied without having to worry about pointers
 * becoming in correct to heap data.
 */
template <class DataTypeReturned>
auto create_data_in_message(MessageHeader& message_header)
    -> DataTypeReturned* {
  if ((reinterpret_cast<std::uintptr_t>(data_location(message_header)) %
       alignof(DataTypeReturned)) != 0) {
    throw std::runtime_error{
        "Unable to convert data at address to the requested type because the "
        "alignment of the requested type is stricter than the alignment of the "
        "data. This means it is not possible to do a safe conversion or to "
        "have the data be interpreted as the requested type. The alignment of "
        "the type is " +
        std::to_string(alignof(DataTypeReturned)) +
        std::string{" and the memory address is " +
                    std::to_string(reinterpret_cast<std::uintptr_t>(
                        data_location(message_header)))}};
  }
  DataTypeReturned* data =
      new (rts::data_location(message_header)) DataTypeReturned{};
  return data;
}

/*!
 * \brief Converts the internal data pointer from the message header to the
 * desired type.
 *
 * This does not do any deserialization, it just does a `reinterpret_cast` of
 * the pointer. It is up to the user to make sure the conversion is safe.
 *
 * We verify that the alignment of the type and the alignment of the data
 * agree, and if not throw a `std::runtime_error`.
 */
template <class DataTypeReturned>
auto data_from_message(MessageHeader& message_header) -> DataTypeReturned* {
  if ((reinterpret_cast<std::uintptr_t>(data_location(message_header)) %
       alignof(DataTypeReturned)) != 0) {
    throw std::runtime_error{
        "Unable to convert data at address to the requested type because the "
        "alignment of the requested type is stricter than the alignment of the "
        "data. This means it is not possible to do a safe conversion or to "
        "have the data be interpreted as the requested type. The alignment of "
        "the type is " +
        std::to_string(alignof(DataTypeReturned)) +
        std::string{" and the memory address is " +
                    std::to_string(reinterpret_cast<std::uintptr_t>(
                        data_location(message_header)))}};
  }
  return reinterpret_cast<DataTypeReturned*>(data_location(message_header));
}
}  // namespace rts
