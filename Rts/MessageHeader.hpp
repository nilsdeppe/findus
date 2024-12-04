// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstdint>

#include "Rts/Detail/MemberFunctionPtr.hpp"

namespace rts {
/*!
 * \brief The header of every message sent between nodes. This is used to
 * identify which distributed object will have the function invoked on it.
 *
 * TODO: explain more?
 */
struct alignas(64) MessageHeader {
  detail::MemberFunctionPtr member_function_ptr = {};
  std::uint64_t collection_index = 0;
  std::uint64_t number_of_bytes_in_message = 0;
  std::uint32_t distributed_object_index = 0;
  char* serialized_data = nullptr;
};
}  // namespace rts
