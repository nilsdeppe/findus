// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>

namespace rts::hardware_info {
#ifdef __cpp_lib_hardware_interference_size
/// \brief Minimum offset between two objects to avoid false sharing.
///
/// Set to 64 bytes if `std::hardware_destructive_interference_size` is not
/// defined. This can be refined for different hardware if necessary.
static constexpr std::size_t hardware_destructive_interference_size =
    std::hardware_destructive_interference_size;
#else
/// \brief Minimum offset between two objects to avoid false sharing.
///
/// Set to 64 bytes if `std::hardware_destructive_interference_size` is not
/// defined. This can be refined for different hardware if necessary.
static constexpr std::size_t hardware_destructive_interference_size = 64;
#endif

/*!
 * \brief Info about the cache.
 */
struct CacheInfo {
  /// The level of the cache, 1, 2, or 3 representing the L1, L2, or L3 cache.
  uint64_t level;
  /// The size of the cache in bytes.
  uint64_t size;
  /// The linesize of the cache in bytes.
  ///
  /// This can be particularly important for parallel programming to avoid false
  /// sharing.
  uint64_t linesize;
};

namespace detail {
std::array<CacheInfo, 3> cache_info();
}  // namespace detail

/*!
 * \brief Get cache size and linesize in bytes for either the level 1, 2, or 3
 * cache.
 *
 * \note `1 <= level <= 3` is required.
 */
CacheInfo cache_info(size_t level);
}  // namespace rts::hardware_info
