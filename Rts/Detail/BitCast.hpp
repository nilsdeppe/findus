
#pragma once

#if defined(__cpp_lib_bit_cast) and (__cpp_lib_bit_cast >= 201806L) and \
    __has_include(<bit>)
#include <bit>
#endif
#include <cstring>
#include <type_traits>

namespace rts::detail {
#if defined(__cpp_lib_bit_cast) and (__cpp_lib_bit_cast >= 201806L)
using std::bit_cast;
#else
template <class To, class From>
std::enable_if_t<sizeof(To) == sizeof(From) and
                     std::is_trivially_copyable_v<From> and
                     std::is_trivially_copyable_v<To>,
                 To>
bit_cast(const From& src) noexcept {
  static_assert(std::is_trivially_constructible_v<To>,
                "This implementation additionally requires "
                "destination type to be trivially constructible");

  To dst;
  std::memcpy(&dst, &src, sizeof(To));
  return dst;
}
#endif
}  // namespace rts::detail
