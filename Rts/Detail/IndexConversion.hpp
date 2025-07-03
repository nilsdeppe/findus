// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstdint>
#include <memory>

namespace rts::detail {
/*!
 * \brief Converts a user-defined collection index to an internal 64-bit
 * representation.
 *
 * This function reinterprets a user-defined collection index as a 64-bit
 * unsigned integer, which is used internally by the runtime system to represent
 * collection indices in a generic way.
 *
 * \tparam IndexType The type of the user-defined collection index.
 * \param collection_index The user-defined collection index to convert.
 * \return The collection index as a 64-bit unsigned integer.
 *
 * \note The user-defined collection index type must be exactly 64 bits in size.
 * \warning This function uses reinterpret_cast and assumes the memory layout is
 * compatible.
 */
template <class IndexType>
std::uint64_t to_internal(const IndexType& collection_index) {
  return *reinterpret_cast<const std::uint64_t*>(
      std::addressof(collection_index));
}

/*!
 * \brief Converts an internal 64-bit collection index to the user-defined
 * collection index type.
 *
 * This function reinterprets a 64-bit unsigned integer (used internally to
 * represent collection indices) as the user-defined collection index type for
 * the specified parallel component. This is typically used to convert indices
 * stored in a generic format back to their original, user-specified type.
 *
 * \tparam ParallelComponent The parallel component type that defines the
 * collection index type.
 * \param collection_index The internal 64-bit collection index to convert.
 * \return The collection index in the user-defined type for the given parallel
 * component.
 *
 * \note The user-defined collection index type must be exactly 64 bits in size.
 * \warning This function uses `reinterpret_cast` and assumes the memory layout
 * is compatible.
 */
template <class ParallelComponent>
typename ParallelComponent::rts_collection_index from_internal(
    const std::uint64_t& collection_index) {
  return *reinterpret_cast<
      const typename ParallelComponent::rts_collection_index*>(
      std::addressof(collection_index));
}
}  // namespace rts::detail
