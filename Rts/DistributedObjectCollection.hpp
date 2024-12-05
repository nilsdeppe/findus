// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <type_traits>

#include "Rts/DistributedObject.hpp"

namespace rts {
template <class ParallelComponent>
class DistributedObjectCollection
    : public DistributedObject<ParallelComponent> {
 public:
  DistributedObjectCollection() {
    static_assert(
        std::is_base_of_v<DistributedObjectCollection<ParallelComponent>,
                          ParallelComponent>,
        "The ParallelComponent must inherit from "
        "DistributedObject<ParallelComponent>");
  }
  ~DistributedObjectCollection() override = default;

  using DistributedObject<ParallelComponent>::DistributedObject;
};
}  // namespace rts
