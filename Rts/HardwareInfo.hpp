// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>  // for hardware_destructive_interference_size

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

/*!
 * \brief Get cache size and linesize in bytes for either the level 1, 2, or 3
 * cache.
 *
 * \note `1 <= level <= 3` is required.
 */
CacheInfo cache_info(size_t level);

/*!
 * \brief Info about the CPU(s) on the physical node.
 */
struct CpuInfo {
  /// \brief The number of processors on the physical node.
  int number_of_processors;
  /// \brief The number of NUMA (Non-Uniform Memory Access) nodes on the
  /// physical node.
  int number_of_numa_nodes;
  /// \brief The number of cores on the physical node.
  int number_of_cores;
  /// \brief The number of processing units/hyper threads/simultaneous
  /// multithreading threads on the physical node.
  int number_of_processing_units;
};

/*!
 * \brief Get info about the CPU(s) on the physical node.
 */
CpuInfo cpu_info();

/*!
 * \brief Binds/pins the current thread to the specified core. This is sometimes
 * called "affinity".
 *
 * If the core ID is larger than the number of cores on the node an exception is
 * throw.
 */
void bind_current_thread_to_core(size_t core_id);
}  // namespace rts::hardware_info
