// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <unordered_set>

#include "findus/Serialize/Serializer.hpp"
#include "findus/Serialize/Stl/Associative.hpp"

namespace findus::serialize {
/*!
 * \brief Serializes or deserializes a std::unordered_set using the Serializer.
 *
 * Each element in the unordered_set is serialized or deserialized individually
 * using operator|. The order of elements is preserved during serialization and
 * deserialization.
 *
 * During unpacking, the unordered_set is reconstructed with all its elements.
 *
 * \tparam Key The type of elements in the unordered_set.
 * \tparam Hash The hash function object type.
 * \tparam KeyEqual The key equivalence function object type.
 * \tparam Allocator The allocator type for the unordered_set.
 * \param serializer The Serializer instance.
 * \param unordered_set The unordered_set to serialize or deserialize.
 * \return Reference to the Serializer.
 */
template <class Key, class Hash, class KeyEqual, class Allocator>
std::enable_if_t<is_serializable_v<Key>, Serializer&> operator|(
    Serializer& serializer,
    std::unordered_set<Key, Hash, KeyEqual, Allocator>& unordered_set) {
  return detail::associative_set_impl<true>(serializer, unordered_set);
}

/*!
 * \brief Serializes or deserializes a std::unordered_multiset using the
 * Serializer.
 *
 * Each element in the unordered_multiset is serialized or deserialized
 * individually using operator|. The order of elements is preserved during
 * serialization and deserialization.
 *
 * During unpacking, the unordered_multiset is reconstructed with all its
 * elements.
 *
 * \tparam Key The type of elements in the unordered_multiset.
 * \tparam Hash The hash function object type.
 * \tparam KeyEqual The key equivalence function object type.
 * \tparam Allocator The allocator type for the unordered_multiset.
 * \param serializer The Serializer instance.
 * \param unordered_multiset The unordered_multiset to serialize or deserialize.
 * \return Reference to the Serializer.
 */
template <class Key, class Hash, class KeyEqual, class Allocator>
std::enable_if_t<is_serializable_v<Key>, Serializer&> operator|(
    Serializer& serializer,
    std::unordered_multiset<Key, Hash, KeyEqual, Allocator>&
        unordered_multiset) {
  return detail::associative_set_impl<true>(serializer, unordered_multiset);
}
}  // namespace findus::serialize
