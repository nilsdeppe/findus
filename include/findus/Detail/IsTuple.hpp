// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <tuple>
#include <type_traits>

namespace findus::detail {
/*!
 * \brief Trait to detect whether a type is `std::tuple`.
 *
 * \tparam T Type being inspected.
 * \details This trait yields `true` for `std::tuple` instantiations and
 *          `false` otherwise. The helper variable template provides
 *          easy access to the constant boolean result.
 */
template <class T>
struct is_std_tuple : std::false_type {};

template <class... Ts>
struct is_std_tuple<std::tuple<Ts...>> : std::true_type {};

/*!
 * \brief Boolean helper yielding `true` when `T` is a `std::tuple`.
 *
 * \tparam T Type being inspected.
 * \details This variable template forwards to `is_std_tuple` so the result
 *          can be obtained without accessing `::value`.
 */
template <class T>
inline constexpr bool is_std_tuple_v = is_std_tuple<T>::value;
}  // namespace findus::detail
