// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <map>

#include "Rts/Serialize/Serializer.hpp"
#include "Rts/Serialize/Stl/Associative.hpp"
#include "Rts/Serialize/Stl/Pair.hpp"

namespace findus::serialize {
/*!
 * \brief Serializes or deserializes a std::map using the Serializer.
 *
 * Each element in the map is serialized or deserialized individually using
 * operator|. The order of elements is preserved during serialization and
 * deserialization.
 *
 * During unpacking, the map is reconstructed with all its elements.
 *
 * \tparam Key The type of keys in the map.
 * \tparam T The type of elements in the map.
 * \tparam Compare The comparison function object type.
 * \tparam Allocator The allocator type for the map.
 * \param serializer The Serializer instance.
 * \param map The map to serialize or deserialize.
 * \return Reference to the Serializer.
 */
template <class Key, class T, class Compare, class Allocator>
std::enable_if_t<is_serializable_v<Key> and is_serializable_v<T>, Serializer&>
operator|(Serializer& serializer, std::map<Key, T, Compare, Allocator>& map) {
  return detail::associative_map_impl<false>(serializer, map);
}

/*!
 * \brief Serializes or deserializes a std::multimap using the Serializer.
 *
 * Each element in the multimap is serialized or deserialized individually using
 * operator|. The order of elements is preserved during serialization and
 * deserialization.
 *
 * During unpacking, the multimap is reconstructed with all its elements.
 *
 * \tparam Key The type of keys in the multimap.
 * \tparam T The type of elements in the multimap.
 * \tparam Compare The comparison function object type.
 * \tparam Allocator The allocator type for the multimap.
 * \param serializer The Serializer instance.
 * \param multimap The multimap to serialize or deserialize.
 * \return Reference to the Serializer.
 */
template <class Key, class T, class Compare, class Allocator>
std::enable_if_t<is_serializable_v<Key> and is_serializable_v<T>, Serializer&>
operator|(Serializer& serializer,
          std::multimap<Key, T, Compare, Allocator>& multimap) {
  return detail::associative_map_impl<false>(serializer, multimap);
}
}  // namespace findus::serialize
