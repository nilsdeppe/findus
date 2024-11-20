// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstdint>

#include "Rts/ActionState.hpp"

namespace rts {
class DistributedObjectBase {
 public:
  virtual ~DistributedObjectBase() = default;
  virtual ActionState invoke_action(uint32_t function_index,
                                    char* serialized_data) = 0;
};
}  // namespace rts
