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
}  // namespace rts::reduction
