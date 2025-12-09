// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <type_traits>

#include "findus/DistributedObject.hpp"

namespace findus {
/*!
 *
 */
template <class ParallelComponent>
class DistributedObjectCollection
    : public DistributedObject<ParallelComponent> {
 public:
  DistributedObjectCollection();
  ~DistributedObjectCollection() override;

  using DistributedObject<ParallelComponent>::DistributedObject;
};

template <class ParallelComponent>
DistributedObjectCollection<ParallelComponent>::DistributedObjectCollection() {
  static_assert(
      std::is_base_of_v<DistributedObjectCollection<ParallelComponent>,
                        ParallelComponent>,
      "The ParallelComponent must inherit from "
      "DistributedObject<ParallelComponent>");
}

template <class ParallelComponent>
DistributedObjectCollection<ParallelComponent>::~DistributedObjectCollection() =
    default;
}  // namespace findus
