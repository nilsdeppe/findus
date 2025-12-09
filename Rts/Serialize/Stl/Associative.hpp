// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <tuple>
#include <type_traits>
#include <utility>

#include "Rts/Serialize/Serializer.hpp"
#include "Rts/Serialize/Stl/Pair.hpp"

namespace findus::serialize::detail {
template <bool IsUnordered, class T>
Serializer& associative_map_impl(Serializer& s, T& container) {
  static_assert(not std::is_const_v<T>);

  using KeyType = typename T::key_type;
  using MappedType = typename T::mapped_type;
  size_t size = container.size();
  s | size;
  if (s.isUnpacking()) {
    for (size_t i = 0; i < size; ++i) {
      static_assert((is_serializer_constructible_v<KeyType> and
                     is_serializer_constructible_v<MappedType>) or
                    is_serializer_constructible_v<MappedType> or
                    std::is_default_constructible_v<MappedType>);
      if constexpr (is_serializer_constructible_v<KeyType> and
                    is_serializer_constructible_v<MappedType>) {
        container.emplace(std::piecewise_construct, std::forward_as_tuple(s),
                          std::forward_as_tuple(s));
      } else if constexpr (is_serializer_constructible_v<MappedType>) {
        KeyType key{};
        s | key;
        container.emplace(std::piecewise_construct,
                          std::forward_as_tuple(std::move(key)),
                          std::forward_as_tuple(s));
      } else if constexpr (std::is_default_constructible_v<MappedType>) {
        KeyType key{};
        s | key;
        MappedType value{};
        s | value;
        container.emplace(std::piecewise_construct,
                          std::forward_as_tuple(std::move(key)),
                          std::forward_as_tuple(std::move(value)));
      }
    }
  } else {
    for (typename T::iterator it = container.begin(); it != container.end();
         ++it) {
      // Because some containers like std::set always have constant iterators,
      // we need to cast away that constness.
      s | const_cast<KeyType&>(it->first);
      s | it->second;
    }
  }
  return s;
}

template <bool IsUnordered, class T>
Serializer& associative_set_impl(Serializer& s, T& container) {
  static_assert(not std::is_const_v<T>);
  size_t size = container.size();
  s | size;
  if (s.isUnpacking()) {
    using ValueType = typename T::value_type;
    for (size_t i = 0; i < size; ++i) {
      if constexpr (is_serializer_constructible_v<ValueType>) {
        container.emplace(s);
      } else {
        ValueType t{};
        s | t;
        container.emplace(std::move(t));
      }
    }
  } else {
    for (typename T::iterator it = container.begin(); it != container.end();
         ++it) {
      // Because some containers like std::set always have constant iterators,
      // we need to cast away that constness.
      s | const_cast<typename T::reference>(*it);
    }
  }
  return s;
}

}  // namespace findus::serialize::detail
