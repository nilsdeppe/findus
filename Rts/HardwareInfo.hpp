// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <array>
#include <cstddef>
#include <hwloc.h>

namespace rts::hardware_info {
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
inline CacheInfo cache_info(const size_t level) {
  assert(level > 0);
  assert(level <= 3);
  static const auto info = detail::cache_info();
  return info[level - 1];
}
}  // namespace rts::hardware_info
