// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstddef>
#include <initializer_list>
#include <tuple>
#include <type_traits>
#include <utility>

#include "findus/Serialize/Serializer.hpp"

namespace findus::serialize {
namespace detail {
/*!
 * \brief Helper function to serialize or deserialize each element of a tuple.
 *
 * This function uses metaprogramming to iterate over the tuple elements
 * and applies operator| to each element in order. The use of
 * std::initializer_list guarantees left-to-right evaluation and thus ensures
 * data is serialized and deserialized consistently.
 *
 * \tparam Ts The types of the elements in the tuple.
 * \tparam Is The index sequence for tuple elements.
 * \param serializer The Serializer instance.
 * \param tuple The tuple to serialize or deserialize.
 */
template <class... Ts, size_t... Is>
void serialize_tuple(Serializer& serializer, std::tuple<Ts...>& tuple,
                     std::index_sequence<Is...> /*metaprogramming*/) {
  static_assert(sizeof...(Ts) == sizeof...(Is));
  (void)std::initializer_list<char>{[&serializer, &tuple](auto index) -> char {
    serializer | std::get<decltype(index)::value>(tuple);
    return '0';
  }(std::integral_constant<size_t, Is>{})...};
}
}  // namespace detail

template <class... Ts>
struct as_bytes<std::tuple<Ts...>>
    : std::bool_constant<(... and serialize_as_bytes_v<Ts>)> {};

/*!
 * \brief Serializes or deserializes a std::tuple using the Serializer.
 *
 * Each element of the tuple is serialized or deserialized individually using
 * operator|. The function supports tuples of arbitrary length and types.
 *
 * During unpacking, all elements of the tuple are restored in order.
 *
 * \tparam Ts The types of the elements in the tuple.
 * \param serializer The Serializer instance.
 * \param tuple The tuple to serialize or deserialize.
 * \return Reference to the Serializer.
 */
template <class... Ts>
std::enable_if_t<(... and is_serializable_v<Ts>), Serializer&> operator|(
    Serializer& serializer, std::tuple<Ts...>& tuple) {
  if constexpr (serialize_as_bytes_v<std::tuple<Ts...>>) {
    serializer(View{reinterpret_cast<std::byte*>(std::addressof(tuple)),
                    sizeof(tuple)});
  } else {
    detail::serialize_tuple(serializer, tuple,
                            std::make_index_sequence<sizeof...(Ts)>{});
  }
  return serializer;
}
}  // namespace findus::serialize

#ifdef FINDUS_MIMIC_CHARM_PUPER
namespace PUP {
template <class... Ts>
std::enable_if_t<(... and findus::serialize::is_serializable_v<Ts>)> operator|(
    PUP::er& p, std::tuple<Ts...>& tuple) {
  findus::serialize::operator|(static_cast<findus::serialize::Serializer&>(p),
                               tuple);
}
}  // namespace PUP
#endif
