// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstdint>

namespace rts {
/*!
 * \brief The header of every message sent between nodes. This is used to
 * identify which distributed object will have the function invoked on it.
 *
 * TODO: explain more?
 */
struct alignas(32) MessageHeader {
  std::uint32_t class_index = 0;
  std::uint32_t function_index = 0;
  std::uint32_t array_index_buffer[6] = {0, 0, 0, 0, 0, 0};
};
}  // namespace rts
