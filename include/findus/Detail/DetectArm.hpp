// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

namespace findus::detail::arm {

#if defined(__ARM_FEATURE_ATOMICS)
#define FINDUS_ARM_LSE
#endif

#if defined(__ARM_ARCH) && __ARM_ARCH >= 804
#define FINDUS_ARM_LSE2
#endif

#if defined(__ARM_ARCH) && __ARM_ARCH >= 904
#define FINDUS_ARM_LSE128
#endif

#if defined(__ARM_FEATURE_RCPC) && __ARM_FEATURE_RCPC >= 3
#define FINDUS_ARM_RCPC3
#endif

struct Atomic128Features {
  bool lse    = false;
  bool lse2   = false;
  bool lse128 = false;
  bool rcpc3  = false;
};

// Cached singleton for repeated access
const Atomic128Features& atomic128_features();
} // namespace arm
