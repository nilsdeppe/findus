// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <type_traits>

#include "Rts/Detail/DistributedObjectBase.hpp"

namespace findus {
template <class ParallelComponent>
class DistributedObject : public detail::DistributedObjectBase {
 public:
  DistributedObject();
  ~DistributedObject() override = default;
};

template <class ParallelComponent>
DistributedObject<ParallelComponent>::DistributedObject() {
  static_assert(std::is_base_of_v<DistributedObject<ParallelComponent>,
                                  ParallelComponent>,
                "The ParallelComponent must inherit from "
                "DistributedObject<ParallelComponent>");
}
}  // namespace findus
