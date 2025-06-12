// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "HardwareInfo.hpp"

#include <array>
#include <hwloc.h>
#include <string>

#include "Rts/Exceptions/Exception.hpp"

namespace rts::hardware_info::detail {
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
  return info;
}
}  // namespace rts::hardware_info::detail
