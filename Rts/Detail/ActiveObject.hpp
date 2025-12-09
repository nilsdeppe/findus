// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstdint>
#include <iosfwd>
#include <limits>

#include "Rts/HardwareInfo.hpp"

namespace findus::detail {
/*!
 * \brief Stores info for indexing the active distributed object and,
 * optionally, its target collection index.
 *
 * The ActiveObject struct holds the index of a distributed object and,
 * optionally, the target collection index. It is aligned to the hardware's
 * destructive interference size to minimize false sharing in multi-threaded
 * environments.
 *
 * Members:
 * - distributed_object_index: The index of the distributed object.
 * - target_collection_index: The index of the target collection element.
 */
struct alignas(findus::hardware_info::hardware_destructive_interference_size)
    ActiveObject {
  std::uint32_t distributed_object_index{
      std::numeric_limits<std::uint32_t>::max()};
  std::uint64_t target_collection_index{
      std::numeric_limits<std::uint64_t>::max()};
};

/// \brief Stream operator for findus::detail::ActiveObject.
std::ostream& operator<<(std::ostream& os, const ActiveObject& obj);
}  // namespace findus::detail
