// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstddef>
#include <iosfwd>

namespace rts::detail {
/*!
 * \brief Used to index the variant in
 * DistributedTaskDriver::DistributedOjectClassHolder
 */
enum DistributedObjectIndex : std::size_t {
  Regular = 0,
  Collection = 1,
  Singleton = 2
};

/// \brief Stream operator for DistributedObjectIndex.
std::ostream& operator<<(std::ostream& os, const DistributedObjectIndex index);
}  // namespace rts::detail
