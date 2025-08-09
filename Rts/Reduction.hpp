// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>

#include "Rts/Message.hpp"
#include "Rts/MessageHeader.hpp"

namespace rts::reduction {
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
  Complete,

  /*!
   * \brief The reduction operation could not be performed because there are
   * too many simultaneous reductions.
   */
  AtCapacity
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

namespace detail {
template <class BinaryOp, class Data_t, size_t... Is>
void combine_impl(Message_t& message0, const Message_t& message1,
                  std::index_sequence<Is...> /*meta*/) {
  const Data_t& message1_data =
      *data_from_message<Data_t>(*message1.get_header());
  Data_t& message0_data = *data_from_message<Data_t>(*message0.get_header());
  BinaryOp{}(message0_data, std::get<Is>(message1_data)...);
}

/*!
 * \brief Combines the reduction data from two messages using a binary
 * operation.
 *
 * This function merges the data from `message1` into `message0` using the
 * specified binary operation (`BinaryOp`). The data in both messages must not
 * have been serialized (i.e., must be in-place constructed). The reduction IDs
 * in both messages must match, otherwise an exception is thrown.
 *
 * \tparam BinaryOp The binary operation to use for combining the data.
 * \tparam Data_t The type of the data tuple stored in the message.
 * \param message0 The message whose data will be updated in-place.
 * \param message1 The message whose data will be combined into message0.
 *
 * \throws Exception if the data in either message was serialized or if the
 *         reduction IDs do not match.
 */
template <class BinaryOp, class Data_t>
void combine(Message_t& message0, const Message_t& message1) {
  if (message0.get_header()->data_was_serialized() or
      message1.get_header()->data_was_serialized()) {
    throw Exception{"Cannot currently combine data that was serialized."};
  }
  const std::uint64_t reduction_id0 = get_id(message0);
  const std::uint64_t reduction_id1 = get_id(message1);
  if (reduction_id0 != reduction_id1) {
    throw Exception{
        "The reduction id in the two reduction messages must match but "
        "message0 has: " +
        std::to_string(reduction_id0) +
        " and message1 has: " + std::to_string(reduction_id1)};
  }
  combine_impl<BinaryOp, Data_t>(
      message0, message1,
      std::make_index_sequence<std::tuple_size_v<Data_t>>{});
}
}  // namespace detail
}  // namespace rts::reduction
