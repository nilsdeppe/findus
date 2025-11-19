// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/Detail/IsTuple.hpp"

#include <tuple>

static_assert(rts::detail::is_std_tuple_v<std::tuple<int, double>>);
static_assert(rts::detail::is_std_tuple_v<std::tuple<int>>);
static_assert(rts::detail::is_std_tuple_v<std::tuple<>>);
static_assert(rts::detail::is_std_tuple_v<std::tuple<std::tuple<int>, double>>);
static_assert(not rts::detail::is_std_tuple_v<int>);
static_assert(not rts::detail::is_std_tuple_v<std::pair<int, int>>);
