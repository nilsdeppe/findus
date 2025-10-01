// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <unordered_map>

#include "Rts/Serialize/Serializer.hpp"
#include "Rts/Serialize/Stl/Associative.hpp"
#include "Rts/Serialize/Stl/Pair.hpp"

namespace rts::serialize {
/*!
 * \brief Serializes or deserializes a std::unordered_map using the Serializer.
 *
 * Each element in the unordered_map is serialized or deserialized individually
 * using operator|. The order of elements is preserved during serialization and
 * deserialization.
 *
 * During unpacking, the unordered_map is reconstructed with all its elements.
 *
 * \tparam Key The type of keys in the unordered_map.
 * \tparam T The type of elements in the unordered_map.
 * \tparam Hash The hash function object type.
 * \tparam KeyEqual The key equivalence function object type.
 * \tparam Allocator The allocator type for the unordered_map.
 * \param serializer The Serializer instance.
 * \param unordered_map The unordered_map to serialize or deserialize.
 * \return Reference to the Serializer.
 */
template <class Key, class T, class Hash, class KeyEqual, class Allocator>
std::enable_if_t<is_serializable_v<Key> and is_serializable_v<T>, Serializer&>
operator|(
    Serializer& serializer,
    std::unordered_map<Key, T, Hash, KeyEqual, Allocator>& unordered_map) {
  return detail::associative_map_impl<true>(serializer, unordered_map);
}

/*!
 * \brief Serializes or deserializes a std::multiunordered_map using the
 * Serializer.
 *
 * Each element in the multiunordered_map is serialized or deserialized
 * individually using operator|. The order of elements is preserved during
 * serialization and deserialization.
 *
 * During unpacking, the multiunordered_map is reconstructed with all its
 * elements.
 *
 * \tparam Key The type of keys in the multiunordered_map.
 * \tparam T The type of elements in the multiunordered_map.
 * \tparam Hash The hash function object type.
 * \tparam KeyEqual The key equivalence function object type.
 * \tparam Allocator The allocator type for the multiunordered_map.
 * \param serializer The Serializer instance.
 * \param unordered_multimap The multiunordered_map to serialize or deserialize.
 * \return Reference to the Serializer.
 */
template <class Key, class T, class Hash, class KeyEqual, class Allocator>
std::enable_if_t<is_serializable_v<Key> and is_serializable_v<T>, Serializer&>
operator|(Serializer& serializer,
          std::unordered_multimap<Key, T, Hash, KeyEqual, Allocator>&
              unordered_multimap) {
  return detail::associative_map_impl<true>(serializer, unordered_multimap);
}
}  // namespace rts::serialize
