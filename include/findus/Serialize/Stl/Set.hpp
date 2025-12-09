// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <set>

#include "findus/Serialize/Serializer.hpp"
#include "findus/Serialize/Stl/Associative.hpp"

namespace findus::serialize {
/*!
 * \brief Serializes or deserializes a std::set using the Serializer.
 *
 * Each element in the set is serialized or deserialized individually using
 * operator|. The order of elements is preserved during serialization and
 * deserialization.
 *
 * During unpacking, the set is reconstructed with all its elements.
 *
 * \tparam Key The type of elements in the set.
 * \tparam Compare The comparison function object type.
 * \tparam Allocator The allocator type for the set.
 * \param serializer The Serializer instance.
 * \param set The set to serialize or deserialize.
 * \return Reference to the Serializer.
 */
template <class Key, class Compare, class Allocator>
std::enable_if_t<is_serializable_v<Key>, Serializer&> operator|(
    Serializer& serializer, std::set<Key, Compare, Allocator>& set) {
  return detail::associative_set_impl<false>(serializer, set);
}

/*!
 * \brief Serializes or deserializes a std::multiset using the Serializer.
 *
 * Each element in the multiset is serialized or deserialized individually using
 * operator|. The order of elements is preserved during serialization and
 * deserialization.
 *
 * During unpacking, the multiset is reconstructed with all its elements.
 *
 * \tparam Key The type of elements in the multiset.
 * \tparam Compare The comparison function object type.
 * \tparam Allocator The allocator type for the multiset.
 * \param serializer The Serializer instance.
 * \param multiset The multiset to serialize or deserialize.
 * \return Reference to the Serializer.
 */
template <class Key, class Compare, class Allocator>
std::enable_if_t<is_serializable_v<Key>, Serializer&> operator|(
    Serializer& serializer, std::multiset<Key, Compare, Allocator>& multiset) {
  return detail::associative_set_impl<false>(serializer, multiset);
}
}  // namespace findus::serialize
