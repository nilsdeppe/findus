// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <type_traits>

#include "Rts/ActionState.hpp"
#include "Rts/DistributedObjectBase.hpp"

namespace rts {
template <class ParallelComponent>
class DistributedObject : public DistributedObjectBase {
 public:
  DistributedObject() {
    static_assert(std::is_base_of_v<DistributedObject<ParallelComponent>,
                                    ParallelComponent>,
                  "The ParallelComponent must inherit from "
                  "DistributedObject<ParallelComponent>");
  }
  ~DistributedObject() override = default;

  ActionState invoke_action(const uint32_t function_index,
                            char* serialized_data) override {
    return {};
  }
};
}  // namespace rts
