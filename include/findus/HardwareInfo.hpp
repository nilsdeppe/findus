// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mpi.h>
#include <new>  // for hardware_destructive_interference_size

namespace findus::hardware_info {
/// \brief Minimum offset between two objects to avoid false sharing.
///
/// Set to 64 bytes if `std::hardware_destructive_interference_size` is not
/// defined. This can be refined for different hardware if necessary.
///
/// \warning std::hardware_destructive_interference_size is not actually
/// portably implemented by GCC so we have to roll our own.
static constexpr std::size_t hardware_destructive_interference_size =
    FINDUS_CACHE_LINE_SIZE;

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
 * \brief Equality operator for CacheInfo.
 * \param lhs The left-hand side CacheInfo.
 * \param rhs The right-hand side CacheInfo.
 * \return True if all fields are equal, false otherwise.
 */
bool operator==(const CacheInfo& lhs, const CacheInfo& rhs);

/*!
 * \brief Inequality operator for CacheInfo.
 * \param lhs The left-hand side CacheInfo.
 * \param rhs The right-hand side CacheInfo.
 * \return True if any field differs, false otherwise.
 */
bool operator!=(const CacheInfo& lhs, const CacheInfo& rhs);

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
 * \brief Equality operator for CpuInfo.
 * \param lhs The left-hand side CpuInfo.
 * \param rhs The right-hand side CpuInfo.
 * \return True if all fields are equal, false otherwise.
 */
bool operator==(const CpuInfo& lhs, const CpuInfo& rhs);

/*!
 * \brief Inequality operator for CpuInfo.
 * \param lhs The left-hand side CpuInfo.
 * \param rhs The right-hand side CpuInfo.
 * \return True if any field differs, false otherwise.
 */
bool operator!=(const CpuInfo& lhs, const CpuInfo& rhs);

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

/*!
 * \brief Prints hardware information for all processes in `comm`.
 *
 * Gathers hardware information including number of processors, NUMA nodes,
 * cores, hardware threads, and cache sizes from all processes in the provided
 * communicator. It then groups processes with identical hardware configurations
 * and prints a summary to standard output. If all processes have the same
 * hardware, a single summary is printed; otherwise, each group of processes
 * with matching hardware is printed separately.
 *
 * Only rank 0 prints the output; other ranks participate in gathering the
 * information.
 *
 * \param comm The MPI communicator over which to gather and print hardware
 * information.
 *
 * \throws findus::MpiException if MPI calls fail.
 * \throws findus::Exception if hardware information cannot be retrieved.
 *
 * \see findus::hardware_info::cpu_info
 * \see findus::hardware_info::cache_info
 */
void print_hardware_info(MPI_Comm comm);
}  // namespace findus::hardware_info
