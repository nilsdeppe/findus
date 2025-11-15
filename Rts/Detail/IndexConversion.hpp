// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <new>

#include "Rts/IsCollection.hpp"

namespace rts::detail {
/*!
 * \brief Helper struct to determine the collection index type for a parallel
 * component.
 *
 * This struct is used internally to select the appropriate type for the
 * collection index based on whether the given parallel component is a
 * collection. If the boolean template parameter `S` is true, the type alias `f`
 * resolves to the `rts_collection_index` type defined by the parallel
 * component. Otherwise, it defaults to `int`.
 *
 * \tparam S Boolean value indicating whether the parallel component is a
 * collection.
 *
 * \see get_collection_index
 */
template <bool S>
struct get_collection_index_impl {
  /*!
   * \brief Type alias for the collection index type.
   *
   * If the parallel component is a collection, this resolves to the
   * `rts_collection_index` type defined by the component.
   *
   * \tparam ParallelComponent The parallel component type.
   */
  template <class ParallelComponent>
  using f = typename ParallelComponent::rts_collection_index;
};

/// \cond
template <>
struct get_collection_index_impl<false> {
  template <class ParallelComponent>
  using f = int;
};
/// \endcond

/*!
 * \brief Type alias for obtaining the collection index type of a parallel
 * component.
 *
 * This type alias resolves to the type used as the collection index for a given
 * parallel component. If the parallel component is a collection, it uses the
 * `rts_collection_index` type defined by the component. Otherwise, it defaults
 * to `int`.
 *
 * \tparam ParallelComponent The parallel component type for which to obtain the
 *         collection index type.
 *
 * \see rts::is_collection
 */
template <class ParallelComponent>
using get_collection_index = typename get_collection_index_impl<
    is_collection_v<ParallelComponent>>::template f<ParallelComponent>;

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
  static_assert(sizeof(IndexType) == sizeof(std::uint64_t));
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
  return *std::launder(
      reinterpret_cast<const typename ParallelComponent::rts_collection_index*>(
          std::addressof(collection_index)));
}
}  // namespace rts::detail
