// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <type_traits>

namespace findus {
/// \cond
template <class ParallelComponent>
class DistributedObjectCollection;
/// \endcond

/// Inherits from `std::true_type` if `ParallelComponent` is a
/// `DistributedObjectCollection`
template <typename ParallelComponent>
struct is_collection
    : std::is_base_of<DistributedObjectCollection<ParallelComponent>,
                      ParallelComponent> {};

/// Is `true` if  `ParallelComponent` is a `DistributedObjectCollection`
template <typename ParallelComponent>
static constexpr bool is_collection_v = is_collection<ParallelComponent>::value;
}  // namespace findus
