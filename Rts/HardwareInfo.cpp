// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "HardwareInfo.hpp"

#include <array>
#include <cassert>
#include <hwloc.h>
#include <string>

#include "Rts/Exceptions/Exception.hpp"

namespace rts::hardware_info {
namespace detail {
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
}  // namespace detail

CacheInfo cache_info(const size_t level) {
  assert(level > 0);
  assert(level <= 3);
  static const auto info = detail::cache_info();
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
    throw Exception("Error calling hwloc_topology_load: " +
                    std::to_string(hwloc_result));
  }
  CpuInfo info{
      hwloc_get_nbobjs_by_type(topology, hwloc_obj_type_t::HWLOC_OBJ_PACKAGE),
      hwloc_get_nbobjs_by_type(topology, hwloc_obj_type_t::HWLOC_OBJ_NUMANODE),
      hwloc_get_nbobjs_by_type(topology, hwloc_obj_type_t::HWLOC_OBJ_CORE),
      hwloc_get_nbobjs_by_type(topology, hwloc_obj_type_t::HWLOC_OBJ_PU)};
  hwloc_topology_destroy(topology);
  return info;
}
}  // namespace

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
    throw Exception("Error calling hwloc_topology_load: " +
                    std::to_string(hwloc_result));
  }
  const int number_of_cores =
      hwloc_get_nbobjs_by_type(topology, hwloc_obj_type_t::HWLOC_OBJ_CORE);

  if (core_id >= number_of_cores) {
    throw Exception{"Cannot bind to core " + std::to_string(core_id) +
                    " because we only have " + std::to_string(number_of_cores) +
                    " cores."};
  }

  const hwloc_obj_t core_to_pin = hwloc_get_obj_by_type(
      topology, hwloc_obj_type_t::HWLOC_OBJ_CORE, core_id);

  if (const auto hwloc_result = hwloc_set_cpubind(topology, core_to_pin->cpuset,
                                                  HWLOC_CPUBIND_THREAD);
      hwloc_result < 0) {
    throw Exception("Error calling hwloc_set_cpubind: " +
                    std::to_string(hwloc_result));
  }

  hwloc_topology_destroy(topology);
}
}  // namespace rts::hardware_info
