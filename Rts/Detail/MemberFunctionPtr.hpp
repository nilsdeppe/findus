// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstdint>
#include <ostream>

#include "Rts/Detail/Endian.hpp"

namespace findus::detail {
/*!
 * \brief Class that represents a pointer to a member function.
 *
 * Member function pointers are 16 bytes of size (other points are 8 bytes in
 * size), which means we need a special structure to work with them. We use
 * two `uint64_t` to store the 16 bytes as a `uint128_t`. We only define
 * addition and subtraction operations on this type (and equivalence) since
 * it is to only be used as a pointer type.
 *
 * The function `findus::detail::to_member_function_ptr()` and
 * `findus::detail::from_member_function_ptr()` are provided to convert between
 * the actual member function pointer and this representation of it.
 *
 * The values are printed as `(upper:lower)`.
 */
struct MemberFunctionPtr {
#ifdef __BIG_ENDIAN__
  uint64_t upper = 0, lower = 0;
  static_assert(false, "Big endian architecture not tested but should work.");
#elif defined(__LITTLE_ENDIAN__)
  uint64_t lower = 0, upper = 0;
#else
  static_assert(false, "Must be either big or little endian.");
#endif
};

inline MemberFunctionPtr operator-(const MemberFunctionPtr& lhs,
                                   const MemberFunctionPtr& rhs) {
  return MemberFunctionPtr{
#ifdef __BIG_ENDIAN__
      lhs.upper - rhs.upper -
          static_cast<uint64_t>((lhs.lower - rhs.lower) > lhs.lower),
      lhs.lower - rhs.lower
#else  // __LITTLE_ENDIAN__
      lhs.lower - rhs.lower,
      lhs.upper - rhs.upper -
          static_cast<uint64_t>((lhs.lower - rhs.lower) > lhs.lower)
#endif
  };
}

inline MemberFunctionPtr operator+(const MemberFunctionPtr& lhs,
                                   const MemberFunctionPtr& rhs) {
  return MemberFunctionPtr{
#ifdef __BIG_ENDIAN__
      lhs.upper + rhs.upper +
          static_cast<uint64_t>((lhs.lower + rhs.lower) < lhs.lower),
      lhs.lower + rhs.lower
#else  // __LITTLE_ENDIAN__
      lhs.lower + rhs.lower,
      lhs.upper + rhs.upper +
          static_cast<uint64_t>((lhs.lower + rhs.lower) < lhs.lower)
#endif
  };
}

inline bool operator==(const MemberFunctionPtr& lhs,
                       const MemberFunctionPtr& rhs) {
  return lhs.lower == rhs.lower and lhs.upper == rhs.upper;
}

inline bool operator!=(const MemberFunctionPtr& lhs,
                       const MemberFunctionPtr& rhs) {
  return not(lhs == rhs);
}

template <class R, class T, class... Args>
MemberFunctionPtr to_member_function_ptr(R (T::*func)(Args...)) {
  static_assert(sizeof(func) == sizeof(MemberFunctionPtr),
                "Internal error. Please file a bug report.");
  union PtrConversionUnion {
    R (T::*f)(Args...);
    MemberFunctionPtr bits;
  };
  PtrConversionUnion u{func};
  return u.bits;
}

template <class R, class T, class... Args>
auto from_member_function_ptr(const MemberFunctionPtr& member_function_ptr)
    -> R (T::*)(Args...) {
  union PtrConversionUnion {
    MemberFunctionPtr bits;
    R (T::*f)(Args...);
  };
  PtrConversionUnion u{member_function_ptr};
  return u.f;
}

inline std::ostream& operator<<(std::ostream& os, const MemberFunctionPtr& t) {
  return os << '(' << t.upper << ':' << t.lower << ')';
}
}  // namespace findus::detail
