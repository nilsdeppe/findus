// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "findus/HardwareInfo.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <hwloc.h>
#include <iomanip>
#include <iostream>
#include <mpi.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "findus/Exceptions/Exception.hpp"
#include "findus/Exceptions/Mpi.hpp"

namespace findus::hardware_info {
namespace {
std::array<CacheInfo, 3> cache_info() {
  hwloc_topology_t topology_;
  if (const auto hwloc_result = hwloc_topology_init(&topology_);
      hwloc_result < 0) {
    throw Exception("Error calling hwloc_topology_init. Received code " +
                    std::to_string(hwloc_result));
  }
  if (const auto hwloc_result = hwloc_topology_load(topology_);
      hwloc_result < 0) {
    throw Exception("Error calling hwloc_topology_load. Received code " +
                    std::to_string(hwloc_result));
  }
  std::array<CacheInfo, 3> info{};
  for (uint64_t i = 1; i <= 3; ++i) {
    hwloc_obj_t cache{};
    switch (i) {
      case 1:
        cache = hwloc_get_obj_by_type(topology_,
                                      hwloc_obj_type_t::HWLOC_OBJ_L1CACHE, 0);
        break;
      case 2:
        cache = hwloc_get_obj_by_type(topology_,
                                      hwloc_obj_type_t::HWLOC_OBJ_L2CACHE, 0);
        break;
      case 3:
        cache = hwloc_get_obj_by_type(topology_,
                                      hwloc_obj_type_t::HWLOC_OBJ_L3CACHE, 0);
        break;
      default:
        throw Exception("Unknown cache level " + std::to_string(i));
    };
    if (hwloc_obj_type_is_cache(cache->type)) {
      info[i - 1] = {i, cache->attr->cache.size, cache->attr->cache.linesize};
    }
  }
  hwloc_topology_destroy(topology_);
  return info;
}
}  // namespace

bool operator==(const CacheInfo& lhs, const CacheInfo& rhs) {
  return lhs.level == rhs.level and lhs.size == rhs.size and
         lhs.linesize == rhs.linesize;
}

bool operator!=(const CacheInfo& lhs, const CacheInfo& rhs) {
  return not(lhs == rhs);
}

CacheInfo cache_info(const size_t level) {
  if (level > 3 or level == 0) {
    throw Exception{"Cache level must be 1, 2, or 3, got " +
                    std::to_string(level)};
  }
  static const auto info = cache_info();
  return info[level - 1];
}

namespace {
CpuInfo cpu_info_impl() {
  hwloc_topology_t topology{};
  if (const auto hwloc_result = hwloc_topology_init(&topology);
      hwloc_result < 0) {
    throw Exception("error calling hwloc_topology_init: " +
                    std::to_string(hwloc_result));
  }
  if (const auto hwloc_result = hwloc_topology_load(topology);
      hwloc_result < 0) {
    hwloc_topology_destroy(topology);
    throw Exception("Error calling hwloc_topology_load: " +
                    std::to_string(hwloc_result));
  }
  hwloc_bitmap_t cpuset = hwloc_bitmap_alloc();
  if (hwloc_get_last_cpu_location(topology, cpuset, HWLOC_CPUBIND_THREAD) < 0) {
    hwloc_bitmap_free(cpuset);
    hwloc_topology_destroy(topology);
    throw Exception("Error calling hwloc_get_last_cpu_location: " +
                    std::to_string(errno));
  }
  const int last_cpu_id = hwloc_bitmap_first(cpuset);

  if (hwloc_get_cpubind(topology, cpuset, HWLOC_CPUBIND_THREAD) < 0) {
    hwloc_bitmap_free(cpuset);
    hwloc_topology_destroy(topology);
    throw Exception("Error calling hwloc_get_cpubind: " +
                    std::to_string(errno));
  }
  const int bound_cpu_id = hwloc_bitmap_first(cpuset);
  hwloc_bitmap_free(cpuset);

  const CpuInfo info{
      hwloc_get_nbobjs_by_type(topology, hwloc_obj_type_t::HWLOC_OBJ_PACKAGE),
      hwloc_get_nbobjs_by_type(topology, hwloc_obj_type_t::HWLOC_OBJ_NUMANODE),
      hwloc_get_nbobjs_by_type(topology, hwloc_obj_type_t::HWLOC_OBJ_CORE),
      hwloc_get_nbobjs_by_type(topology, hwloc_obj_type_t::HWLOC_OBJ_PU),
      last_cpu_id,
      bound_cpu_id};
  if (info.number_of_processors < 1) {
    hwloc_topology_destroy(topology);
    throw Exception{"Error calling hwloc, got fewer than 1 processors: " +
                    std::to_string(info.number_of_processors)};
  }
  if (info.number_of_numa_nodes < 1) {
    hwloc_topology_destroy(topology);
    throw Exception{"Error calling hwloc, got fewer than 1 NUMA nodes: " +
                    std::to_string(info.number_of_numa_nodes)};
  }
  if (info.number_of_cores < 1) {
    hwloc_topology_destroy(topology);
    throw Exception{"Error calling hwloc, got fewer than 1 core: " +
                    std::to_string(info.number_of_cores)};
  }
  if (info.number_of_processing_units < 1) {
    hwloc_topology_destroy(topology);
    throw Exception{
        "Error calling hwloc, got fewer than 1 processing "
        "units/hyperthreads/simultaneous multithreads: " +
        std::to_string(info.number_of_processing_units)};
  }
  hwloc_topology_destroy(topology);
  return info;
}
}  // namespace

bool operator==(const CpuInfo& lhs, const CpuInfo& rhs) {
  return lhs.number_of_processors == rhs.number_of_processors and
         lhs.number_of_numa_nodes == rhs.number_of_numa_nodes and
         lhs.number_of_cores == rhs.number_of_cores and
         lhs.number_of_processing_units == rhs.number_of_processing_units and
         lhs.last_cpu_id == rhs.last_cpu_id and
         lhs.bound_cpu_id == rhs.bound_cpu_id;
}

bool operator!=(const CpuInfo& lhs, const CpuInfo& rhs) {
  return not(lhs == rhs);
}

CpuInfo cpu_info() {
  static auto info = cpu_info_impl();
  return info;
}

void bind_current_thread_to_core(const size_t core_id) {
  // Bind/pin the thread to a CPU core.
  //
  // Modified from:
  // https://github.com/eliben/code-for-blog/blob/master/2016/threads-affinity/hwloc-example.cpp
  hwloc_topology_t topology{};
  if (const auto hwloc_result = hwloc_topology_init(&topology);
      hwloc_result < 0) {
    throw Exception("error calling hwloc_topology_init: " +
                    std::to_string(hwloc_result));
  }
  if (const auto hwloc_result = hwloc_topology_load(topology);
      hwloc_result < 0) {
    hwloc_topology_destroy(topology);
    throw Exception("Error calling hwloc_topology_load: " +
                    std::to_string(hwloc_result));
  }
  const int number_of_cores =
      hwloc_get_nbobjs_by_type(topology, hwloc_obj_type_t::HWLOC_OBJ_CORE);

  if (number_of_cores < 0) {
    hwloc_topology_destroy(topology);
    throw Exception{"hwloc gave a negative number of cores, " +
                    std::to_string(number_of_cores)};
  }

  if (core_id >= static_cast<size_t>(number_of_cores)) {
    hwloc_topology_destroy(topology);
    throw Exception{"Cannot bind to core " + std::to_string(core_id) +
                    " because we only have " + std::to_string(number_of_cores) +
                    " cores."};
  }

  const hwloc_obj_t core_to_pin = hwloc_get_obj_by_type(
      topology, hwloc_obj_type_t::HWLOC_OBJ_CORE, core_id);

  if (const auto hwloc_result = hwloc_set_cpubind(topology, core_to_pin->cpuset,
                                                  HWLOC_CPUBIND_THREAD);
      hwloc_result < 0) {
    hwloc_topology_destroy(topology);
    throw Exception("Error calling hwloc_set_cpubind: " +
                    std::to_string(hwloc_result));
  }

  hwloc_topology_destroy(topology);
}

namespace {
struct GatheredHardwareInfo {
  hardware_info::CpuInfo cpu_info;
  std::array<hardware_info::CacheInfo, 3> cache_info;
};

[[maybe_unused]] bool operator==(const GatheredHardwareInfo& lhs,
                                 const GatheredHardwareInfo& rhs) {
  return lhs.cpu_info == rhs.cpu_info and lhs.cache_info == rhs.cache_info;
}

/*!
 * \brief Converts a sorted list of process IDs into a compact, comma-separated
 * string with ranges.
 *
 * This function takes a sorted vector of process IDs and returns a string where
 * consecutive sequences are represented as ranges (e.g., "0-3,5,7-9" for input
 * [0,1,2,3,5,7,8,9]). Single process IDs are listed individually.
 *
 * \param process_ids A sorted vector of process IDs.
 * \return A comma-separated string with ranges representing the process IDs.
 *
 * \note The input vector must be sorted in ascending order for correct range
 * detection. \note If the input vector is empty, an empty string is returned.
 *
 * \example
 * std::string s = process_ids_to_ranges({0,1,2,3,5,7,8,9}); // s == "0-3,5,7-9"
 */
std::string process_ids_to_ranges(const std::vector<int>& process_ids) {
  if (process_ids.empty()) {
    return "";
  }

  std::string result;
  int range_start = process_ids[0];
  int previous_process_id = process_ids[0];

  for (size_t i = 1; i <= process_ids.size(); ++i) {
    if (i < process_ids.size() and process_ids[i] == previous_process_id + 1) {
      previous_process_id = process_ids[i];
      continue;
    }
    // End of a range
    if (range_start == previous_process_id) {
      result += std::to_string(range_start);
    } else {
      result += std::to_string(range_start) + "-" +
                std::to_string(previous_process_id);
    }
    if (i < process_ids.size()) {
      result += ",";
      range_start = previous_process_id = process_ids[i];
    }
  }
  return result;
}
}  // namespace
}  // namespace findus::hardware_info

template <>
struct std::hash<findus::hardware_info::GatheredHardwareInfo> {
  std::size_t operator()(
      const findus::hardware_info::GatheredHardwareInfo& x) const noexcept {
    return std::hash<std::string_view>{}(
        std::string_view{reinterpret_cast<const char*>(&x),
                         sizeof(findus::hardware_info::GatheredHardwareInfo)});
  }
};

namespace findus::hardware_info {
void print_hardware_info(const MPI_Comm comm) {
  int this_process_id = -1;
  if (MPI_Comm_rank(comm, &this_process_id) != MPI_SUCCESS) {
    throw MpiException("Failed to get node rank when printing hardware info.");
  }
  int number_of_processes = -1;
  if (MPI_Comm_size(comm, &number_of_processes) != MPI_SUCCESS) {
    throw MpiException(
        "Failed to get the number of nodes when printing hardware info.");
  }

  GatheredHardwareInfo local_hardware_info;
  local_hardware_info.cpu_info = hardware_info::cpu_info();
  local_hardware_info.cache_info[0] = hardware_info::cache_info(1);
  local_hardware_info.cache_info[1] = hardware_info::cache_info(2);
  local_hardware_info.cache_info[2] = hardware_info::cache_info(3);

  std::vector<GatheredHardwareInfo> all_info;
  if (this_process_id == 0) {
    all_info.resize(static_cast<size_t>(number_of_processes));
  }

  if (const auto mpi_result = MPI_Gather(
          &local_hardware_info, sizeof(GatheredHardwareInfo), MPI_BYTE,
          all_info.data(), sizeof(GatheredHardwareInfo), MPI_BYTE, 0, comm);
      mpi_result != MPI_SUCCESS) {
    throw MpiException{"Failed Gather of hardware info for print with " +
                       std::to_string(mpi_result)};
  }

  if (this_process_id == 0) {
    std::unordered_map<GatheredHardwareInfo, std::vector<int>> groups{};
    // Note: the process IDs for each GatheredHardwareInfo is guaranteed to be
    // sorted.
    for (int process_id = 0; process_id < number_of_processes; ++process_id) {
      const auto& info = all_info[static_cast<size_t>(process_id)];
      groups[info].push_back(process_id);
    }

    constexpr size_t print_width = 9;
    for (const auto& [group, process_ids] : groups) {
      // Print the hardware info for this group of process IDs.
      std::cout << "findus: Hardware info from processes "
                << process_ids_to_ranges(process_ids)
                << ":\nfindus:   Number of processors:       "
                << std::setw(print_width) << group.cpu_info.number_of_processors
                << "\nfindus:   Number of NUMA nodes:       "
                << std::setw(print_width) << group.cpu_info.number_of_numa_nodes
                << "\nfindus:   Number of cores:            "
                << std::setw(print_width) << group.cpu_info.number_of_cores
                << "\nfindus:   Number of hardware threads: "
                << std::setw(print_width)
                << group.cpu_info.number_of_processing_units
                << "\nfindus:   L1 cache size (kB):         "
                << std::setw(print_width)
                << static_cast<int>(group.cache_info[0].size) / 1024
                << "\nfindus:   L2 cache size (kB):         "
                << std::setw(print_width)
                << static_cast<int>(group.cache_info[1].size) / 1024
                << "\nfindus:   L3 cache size (kB):         "
                << std::setw(print_width)
                << static_cast<int>(group.cache_info[2].size) / 1024 << "\n";
    }
  }
}
}  // namespace findus::hardware_info

#if defined(FINDUS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <doctest/extensions/doctest_mpi.h>

namespace findus::hardware_info {
TEST_CASE("HardwareInfo") {
  CHECK_THROWS_AS(cache_info(4), Exception);
  try {
    cache_info(4);
  } catch (const Exception& e) {
    CHECK(std::string{e.what()} == "Cache level must be 1, 2, or 3, got 4");
  }
  CHECK_THROWS_AS(cache_info(0), Exception);
  try {
    cache_info(0);
  } catch (const Exception& e) {
    CHECK(std::string{e.what()} == "Cache level must be 1, 2, or 3, got 0");
  }
  for (size_t i = 1; i < 4; ++i) {
    const CacheInfo ci = cache_info(i);
    CHECK(ci.level == i);
    CHECK(ci.size > 0);
    // Cache size is likely under 1GB on all systems. Increase in necessary.
    CHECK(ci.size < 1024 * 1024 * 1024);
    CHECK(ci.linesize > 0);
    // Cache size is likely under 1kB on all systems. Increase in necessary.
    CHECK(ci.linesize < 1024);
  }

  CHECK(cpu_info().number_of_processors > 0);
  // Unlikely to have more than 4 processors per node. Increase if necessary.
  CHECK(cpu_info().number_of_processors < 5);
  CHECK(cpu_info().number_of_numa_nodes > 0);
  // Unlikely to have more than 4 NUMA nodes per node. Increase if necessary.
  CHECK(cpu_info().number_of_numa_nodes < 5);
  CHECK(cpu_info().number_of_cores > 0);
  // Unlikely to have more than 1024 cores per node. Increase if necessary.
  CHECK(cpu_info().number_of_cores < 1025);
  CHECK(cpu_info().number_of_processing_units > 0);
  // Unlikely to have more than 2048 PUs per node. Increase if necessary.
  CHECK(cpu_info().number_of_processing_units < 2049);

  bind_current_thread_to_core(1);

  // Unlikely to have 1 million cores. Increase if necessary.
  const size_t core_bind_id = 1000000;
  CHECK_THROWS(bind_current_thread_to_core(core_bind_id));
  try {
    bind_current_thread_to_core(core_bind_id);
  } catch (const Exception& e) {
    CHECK(std::string{e.what()} ==
          "Cannot bind to core " + std::to_string(core_bind_id) +
              " because we only have " +
              std::to_string(cpu_info().number_of_cores) + " cores.");
  }

  const CacheInfo a_cache_info{1, 32768, 64};
  const CacheInfo b_cache_info{1, 32768, 64};
  const CacheInfo c_cache_info{2, 32768, 64};
  const CacheInfo d_cache_info{1, 65536, 64};
  const CacheInfo e_cache_info{1, 32768, 128};

  CHECK(a_cache_info == b_cache_info);
  CHECK_FALSE(a_cache_info != b_cache_info);

  CHECK(a_cache_info != c_cache_info);
  CHECK(a_cache_info != d_cache_info);
  CHECK(a_cache_info != e_cache_info);

  const CpuInfo a_cpu_info{2, 1, 8, 16, 1, 3};
  const CpuInfo b_cpu_info{2, 1, 8, 16, 1, 3};
  const CpuInfo c_cpu_info{4, 1, 8, 16, 1, 3};
  const CpuInfo d_cpu_info{2, 2, 8, 16, 1, 3};
  const CpuInfo e_cpu_info{2, 1, 4, 16, 1, 3};
  const CpuInfo f_cpu_info{2, 1, 8, 8, 1, 3};
  const CpuInfo g_cpu_info{2, 1, 8, 16, 2, 3};
  const CpuInfo h_cpu_info{2, 1, 8, 16, 1, 4};

  CHECK(a_cpu_info == b_cpu_info);
  CHECK_FALSE(a_cpu_info != b_cpu_info);

  CHECK(a_cpu_info != c_cpu_info);
  CHECK(a_cpu_info != d_cpu_info);
  CHECK(a_cpu_info != e_cpu_info);
  CHECK(a_cpu_info != f_cpu_info);
  CHECK(a_cpu_info != g_cpu_info);
  CHECK(a_cpu_info != h_cpu_info);

  CHECK(process_ids_to_ranges({}) == "");
  CHECK(process_ids_to_ranges({5}) == "5");
  CHECK(process_ids_to_ranges({1, 3, 5}) == "1,3,5");
  CHECK(process_ids_to_ranges({1, 2, 3, 5, 7}) == "1-3,5,7");
  CHECK(process_ids_to_ranges({1, 3, 5, 6, 7}) == "1,3,5-7");
  CHECK(process_ids_to_ranges({0, 1, 2, 3, 5, 7, 8, 9}) == "0-3,5,7-9");
  CHECK(process_ids_to_ranges({4, 5, 6, 7}) == "4-7");
  CHECK(process_ids_to_ranges({2, 4, 5, 6, 8}) == "2,4-6,8");
}

MPI_TEST_CASE("HardwareInfoParallel", 2) {
  // Only check output on rank 0, since only rank 0 prints
  if (test_rank == 0) {
    // Redirect std::cout to a stringstream
    std::stringstream buffer;
    std::streambuf* old_cout = std::cout.rdbuf(buffer.rdbuf());

    // Call the function
    findus::hardware_info::print_hardware_info(test_comm);

    // Restore std::cout
    std::cout.rdbuf(old_cout);

    // Get the output
    std::string output = buffer.str();

    // Check that some expected substrings are present
    CHECK(output.find("findus: Hardware info") != std::string::npos);
    CHECK(output.find("findus:   Number of processors:") != std::string::npos);
    CHECK(output.find("findus:   Number of NUMA nodes:") != std::string::npos);
    CHECK(output.find("findus:   Number of cores:") != std::string::npos);
    CHECK(output.find("findus:   Number of hardware threads:") !=
          std::string::npos);
    CHECK(output.find("findus:   L1 cache size (kB):") != std::string::npos);
    CHECK(output.find("findus:   L2 cache size (kB):") != std::string::npos);
    CHECK(output.find("findus:   L3 cache size (kB):") != std::string::npos);
  } else {
    // On other ranks, just call the function (no output to check)
    findus::hardware_info::print_hardware_info(test_comm);
  }
}
}  // namespace findus::hardware_info
#endif
