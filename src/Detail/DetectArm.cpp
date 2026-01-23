// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "findus/Detail/DetectArm.hpp"

#include <cstdint>
#include <optional>

// Platform detection
#if defined(__linux__)
#include <sys/auxv.h>
#define FINDUS_ARM_DETECT_LINUX 1

// HWCAP definitions (for older headers)
//
// From the Linux kernel internals:
// https://github.com/torvalds/linux/blob/master/arch/arm64/include/uapi/asm/hwcap.h
#ifndef HWCAP_ATOMICS
#define HWCAP_ATOMICS (1 << 8)
#endif
#ifndef HWCAP_USCAT
#define HWCAP_USCAT (1 << 25)
#endif
#ifndef HWCAP2_LRCPC3
#define HWCAP2_LRCPC3 (1UL << 46)
#endif
#ifndef HWCAP2_LSE128
#define HWCAP2_LSE128 (1UL << 47)
#endif

#elif defined(__APPLE__)
#include <sys/sysctl.h>
#define FINDUS_ARM_DETECT_DARWIN 1
#elif defined(_WIN32)
#include <windows.h>
#define FINDUS_ARM_DETECT_WINDOWS 1
#endif

namespace findus::detail::arm {
namespace {
constexpr Atomic128Features compile_time_atomic128_features() {
  Atomic128Features f;

#if defined(__ARM_FEATURE_ATOMICS)
  f.lse = true;
#endif

#if defined(__ARM_ARCH) && __ARM_ARCH >= 804
  f.lse2 = true;
#endif

#if defined(__ARM_ARCH) && __ARM_ARCH >= 904
  f.lse128 = true;
#endif

#if defined(__ARM_FEATURE_RCPC) && __ARM_FEATURE_RCPC >= 3
  f.rcpc3 = true;
#endif

  return f;
}

Atomic128Features runtime_atomic128_features() {
  Atomic128Features f;

#if defined(FINDUS_ARM_DETECT_LINUX)
  unsigned long hwcap = getauxval(AT_HWCAP);
  unsigned long hwcap2 = getauxval(AT_HWCAP2);

  f.lse    = (hwcap & HWCAP_ATOMICS) != 0;
  f.lse2   = (hwcap & HWCAP_USCAT) != 0;
  f.lse128 = (hwcap2 & HWCAP2_LSE128) != 0;
  f.rcpc3  = (hwcap2 & HWCAP2_LRCPC3) != 0;

#elif defined(FINDUS_ARM_DETECT_DARWIN)
  const auto check = [](const char* name) {
    int32_t val = 0;
    size_t size = sizeof(val);
    return sysctlbyname(name, &val, &size, nullptr, 0) == 0 and val;
  };
  f.lse    = check("hw.optional.arm.FEAT_LSE");
  f.lse2   = check("hw.optional.arm.FEAT_LSE2");
  f.lse128 = check("hw.optional.arm.FEAT_LSE128");
  f.rcpc3  = check("hw.optional.arm.FEAT_LRCPC3");

#elif defined(FINDUS_ARM_DETECT_WINDOWS)
  f.lse = IsProcessorFeaturePresent(34);
  // LSE2, LSE128, RCPC3 not exposed on Windows
#endif

  return f;
}

// Combined: use compile-time if available, else runtime
Atomic128Features detect() {
  constexpr auto ct = compile_time_atomic128_features();

  // If compiled with full feature support, trust compile-time
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunreachable-code"
#endif
  if constexpr (ct.lse and ct.lse2 and ct.lse128 and ct.rcpc3) {
    return ct;
  } else {

    // Otherwise merge: compile-time guarantees + runtime detection
    Atomic128Features rt = runtime_atomic128_features();
    return {
        .lse = ct.lse or rt.lse,
        .lse2 = ct.lse2 or rt.lse2,
        .lse128 = ct.lse128 or rt.lse128,
        .rcpc3 = ct.rcpc3 or rt.rcpc3,
    };
  }
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}

struct Atomic128FeaturesInternal {
  static const Atomic128Features features;
};

const Atomic128Features Atomic128FeaturesInternal::features = detect();
}

const Atomic128Features& atomic128_features() {
  return Atomic128FeaturesInternal::features;
}
}  // namespace arm
