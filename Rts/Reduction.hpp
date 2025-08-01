// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstdint>

#include "Rts/Message.hpp"

namespace rts::reduction {
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

/*!
 * \brief Indicates the result of attempting to insert or combine reduction
 * data.
 *
 * This enum is used to communicate the outcome of an insert or combine
 * operation in the reduction data handler.
 */
enum class InsertAction {
  /*!
   * \brief A new entry was inserted for the given reduction ID.
   *
   * This value indicates that the reduction data and callback were newly
   * inserted into the handler, as no existing entry for the reduction ID
   * was found.
   */
  Insert,

  /*!
   * \brief The reduction data was combined with an existing entry.
   *
   * This value indicates that an entry for the reduction ID already existed,
   * and the provided data was combined with the existing data using the
   * reduction operation.
   */
  Combine,

  /*!
   * \brief The reduction operation is complete.
   *
   * This value indicates that the reduction has finished and no further
   * insertions or combinations are needed for the given reduction ID.
   */
  Complete
};

/*!
 * \brief Stream insertion operator for InsertAction.
 *
 * Writes a human-readable string representation of the InsertAction enum value
 * to the provided output stream.
 *
 * \param os The output stream.
 * \param action The InsertAction enum value to write.
 * \return The output stream.
 */
std::ostream& operator<<(std::ostream& os, const InsertAction action);
}  // namespace rts::reduction
