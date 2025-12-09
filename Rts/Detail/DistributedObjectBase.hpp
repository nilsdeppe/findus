// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

namespace findus::detail {
/*!
 * \brief The internally used type-erased base class for distributed objects.
 *
 * This should never get exposed to user code since they will always be
 * querying their most derived distributed objects, and internally we generate
 * template functions to do the invocation. This allows us to preserve the
 * type information across the network.
 */
class DistributedObjectBase {
 public:
  virtual ~DistributedObjectBase() = 0;
};
}  // namespace findus::detail
