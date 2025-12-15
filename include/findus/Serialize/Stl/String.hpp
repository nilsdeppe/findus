// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <string>

#include "findus/Serialize/Serializer.hpp"

namespace findus::serialize {
/*!
 * \brief Serializes or deserializes a std::basic_string using the Serializer.
 *
 * The string's size and capacity are serialized first, followed by its
 * character data. During unpacking, the string is reserved to the serialized
 * capacity and resized to the serialized size, then its contents are restored.
 *
 * This function supports all standard string types, such as std::string,
 * std::wstring, std::u16string, and std::u32string.
 *
 * \tparam CharT The character type of the string.
 * \tparam Traits The character traits type.
 * \tparam Allocator The allocator type for the string.
 * \param serializer The Serializer instance.
 * \param string The string to serialize or deserialize.
 * \return Reference to the Serializer.
 */
template <class CharT, class Traits, class Allocator>
Serializer& operator|(Serializer& serializer,
                      std::basic_string<CharT, Traits, Allocator>& string) {
  size_t size = string.size();
  size_t capacity = string.capacity();
  serializer | size | capacity;
  if (serializer.isUnpacking()) {
    string.reserve(capacity);
    string.resize(size);
  }
  return serializer(View{const_cast<CharT*>(string.data()), size});
}
}  // namespace findus::serialize

#ifdef FINDUS_MIMIC_CHARM_PUPER
namespace PUP {
template <class CharT, class Traits, class Allocator>
void operator|(er& p, std::basic_string<CharT, Traits, Allocator>& string) {
  findus::serialize::operator|(static_cast<findus::serialize::Serializer&>(p),
                               string);
}
}  // namespace PUP
#endif
