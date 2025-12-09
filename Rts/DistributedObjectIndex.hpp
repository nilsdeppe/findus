// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstdint>

namespace findus::detail {
/*!
 * \brief Counter used to assign each distributed object a unique integer ID.
 *
 * The function `detail::distributed_object_index()` gives the resulting index
 * for a parallel component.
 */
extern uint32_t distributed_object_index_counter;

/*!
 * \brief Returns the unique ID for the parallel component.
 */
template <typename ParallelComponent>
uint32_t distributed_object_index() {
  static uint32_t index = (distributed_object_index_counter++);
  return index;
}
}  // namespace findus::detail
