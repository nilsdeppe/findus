// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "HardwareInfo.hpp"

#include <array>
#include <hwloc.h>
#include <string>

#include "Rts/Exceptions/Exception.hpp"

namespace rts::hardware_info {
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
  const CpuInfo info{
      hwloc_get_nbobjs_by_type(topology, hwloc_obj_type_t::HWLOC_OBJ_PACKAGE),
      hwloc_get_nbobjs_by_type(topology, hwloc_obj_type_t::HWLOC_OBJ_NUMANODE),
      hwloc_get_nbobjs_by_type(topology, hwloc_obj_type_t::HWLOC_OBJ_CORE),
      hwloc_get_nbobjs_by_type(topology, hwloc_obj_type_t::HWLOC_OBJ_PU)};
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
         lhs.number_of_processing_units == rhs.number_of_processing_units;
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
}  // namespace rts::hardware_info

#if defined(RTS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <string>

#include "Rts/Detail/GetOutput.hpp"

namespace rts::hardware_info {
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

  const CpuInfo a_cpu_info{2, 1, 8, 16};
  const CpuInfo b_cpu_info{2, 1, 8, 16};
  const CpuInfo c_cpu_info{4, 1, 8, 16};
  const CpuInfo d_cpu_info{2, 2, 8, 16};
  const CpuInfo e_cpu_info{2, 1, 4, 16};
  const CpuInfo f_cpu_info{2, 1, 8, 8};

  CHECK(a_cpu_info == b_cpu_info);
  CHECK_FALSE(a_cpu_info != b_cpu_info);

  CHECK(a_cpu_info != c_cpu_info);
  CHECK(a_cpu_info != d_cpu_info);
  CHECK(a_cpu_info != e_cpu_info);
  CHECK(a_cpu_info != f_cpu_info);
}
}  // namespace rts::hardware_info
#endif
