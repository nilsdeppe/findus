// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstdint>

#include "Rts/Message.hpp"

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
