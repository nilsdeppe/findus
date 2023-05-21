// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "HardwareInfo.hpp"

namespace rts::hardware_info::detail {
std::array<CacheInfo, 3> cache_info() {
  hwloc_topology_t topology_;
  if (hwloc_topology_init(&topology_) < 0) {
    throw std::runtime_error("error calling hwloc_topology_init");
  }
  if (hwloc_topology_load(topology_) < 0) {
    throw std::runtime_error("error calling hwloc_topology_load");
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
        throw std::runtime_error("unknown size for cache");
    };
    if (hwloc_obj_type_is_cache(cache->type)) {
      info[i - 1] = {i, cache->attr->cache.size, cache->attr->cache.linesize};
    }
  }
  return info;
}
}  // namespace detail
