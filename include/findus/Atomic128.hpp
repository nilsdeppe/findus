// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>

#include "Detail/BitCast.hpp"
#include "findus/Detail/BitCast.hpp"
#include "findus/Detail/DetectArm.hpp"

namespace findus {
namespace detail {
#if defined(__x86_64__) || defined(_M_X64)
inline bool x86_cpu_supports_16_byte_atomic() {
  std::uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

  // Issue CPUID with eax=1 for feature flags.
  eax = 1;
  // clobber memory to prevent instruction reordering by the compiler since we
  // are likely using this check to prevent against issuing an illegal
  // instruction.
  __asm__ volatile("cpuid"
                   : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   :
                   : "memory");

  // Check AVX bit.
  //
  // XSAVE and OSXSAVE are needed only if the YMM registers are used. However,
  // since SSE2, support for 128-bit register stores are supported. Thus, for
  // 16b atomic we only need to verify AVX support.
  const bool avx = (ecx & (1 << 28)) != 0;
  return avx;
}
#endif
}  // namespace detail

/*!
 * \brief Provides a 128-bit atomic variable for trivially copyable types.
 *
 * The Atomic128 class implements atomic operations for 128-bit data types on
 * supported hardware (currently x86_64 with best performance when AVX support
 * is available). It supports atomic load, store, exchange, and
 * compare-exchange/compare-and-swap operations, similar to std::atomic, but for
 * types that are 16 bytes in size and 16-byte aligned.
 *
 * \tparam T The type to be stored atomically. Must be trivially copyable,
 *           16 bytes in size, and 16-byte aligned.
 *
 * \note This class is only supported on x86_64 architectures with
 *       optimizations when AVX support is available (checked at compile time
 *       and at run time). Attempting to use it on unsupported architectures
 *       will result in a compilation error.
 *
 * \details
 * - The class disables copy and move construction and assignment to ensure
 *   atomicity.
 * - Provides both regular and volatile-qualified member functions for
 *   compatibility with std::atomic.
 * - All operations are lock-free and use hardware atomic instructions.
 * - Memory order semantics are follow those of std::atomic.
 *
 * Example usage:
 *
 * \code
 * struct alignas(16) MyData {
 *   uint64_t a, b;
 * };
 * findus::Atomic128<MyData> atomic_data;
 * MyData value = atomic_data.load();
 * \endcode
 */
template <class T>
class Atomic128 {
 private:
  struct alignas(16) InternalData {
    std::uint64_t low_bits;
    std::uint64_t high_bits;
    bool operator==(const InternalData& rhs) const noexcept {
      return low_bits == rhs.low_bits and high_bits == rhs.high_bits;
    }
    bool operator!=(const InternalData& rhs) const noexcept {
      return not(*this == rhs);
    }
  };

  static_assert(sizeof(InternalData) == 16);
  static_assert(alignof(InternalData) == 16);

 public:
  static_assert(sizeof(T) == 16);
  static_assert(alignof(T) >= 16);
  static_assert(alignof(T) % 16 == 0);
  static_assert(std::is_trivially_copyable_v<T>);
  static_assert(std::is_copy_constructible_v<T>);
  static_assert(std::is_move_constructible_v<T>);
  static_assert(std::is_copy_assignable_v<T>);
  static_assert(std::is_move_assignable_v<T>);
  static_assert(std::is_same_v<T, std::remove_cv_t<T>>);

  /*!
   * \brief Value-initializes the underlying object (i.e. with `T()`). The
   * initialization is not atomic. This overload participates in overload
   * resolution only if `std::is_default_constructible_v<T>` is true.
   */
  constexpr Atomic128() noexcept(std::is_nothrow_default_constructible_v<T>) {
    T desired{};
    data_ = findus::detail::bit_cast<InternalData>(desired);
  }
  /*!
   * \brief Initializes the underlying object with desired. The initialization
   * is not atomic.
   */
  constexpr Atomic128(const T& desired) noexcept {
    data_ = findus::detail::bit_cast<InternalData>(desired);
  }
  /*!
   * \brief Atomic variables are not copy constructible.
   */
  Atomic128(const Atomic128&) = delete;
  /*!
   * \brief Atomic variables are not copy assignable.
   */
  Atomic128& operator=(const Atomic128&) = delete;
  /*!
   * \brief Atomic variables are not move constructible.
   */
  Atomic128(Atomic128&&) = delete;
  /*!
   * \brief Atomic variables are not move assignable.
   */
  Atomic128& operator=(Atomic128&&) = delete;
  ~Atomic128() noexcept = default;

  /// \brief `true`
  static constexpr bool is_always_lock_free = true;

  /// @{
  /// \brief `true`
  bool is_lock_free() const noexcept { return true; }
  bool is_lock_free() const volatile noexcept { return true; }
  /// @}

  /// @{
  /*!
   * \brief Atomically loads and returns the current value of the atomic
   * variable. Memory is affected according to the value of `memory_order`.
   *
   * If `memory_order` is one of `std::memory_order_consume`,
   * `std::memory_order_acquire` and `std::memory_order_acq_rel`, the behavior
   * is undefined.
   *
   * \note `volatile` overloads are provided for compatibility with
   * `std::atomic`.
   */
  T load(const std::memory_order memory_order =
             std::memory_order_seq_cst) const noexcept {
    InternalData internal_result{0, 0};
    load_store_impl<false>(internal_result, data_, memory_order);
    return findus::detail::bit_cast<T>(internal_result);
  }
  T load(const std::memory_order memory_order = std::memory_order_seq_cst) const
      volatile noexcept {
    InternalData internal_result{0, 0};
    load_store_impl<false>(internal_result, data_, memory_order);
    return findus::detail::bit_cast<T>(internal_result);
  }
  /// @}

  /// @{
  /*!
   * \brief Atomically replaces the current value with `desired`. Memory is
   * affected according to the value of `memory_order`.
   *
   * If `memory_order` is one of `std::memory_order_consume`,
   * `std::memory_order_acquire` and `std::memory_order_acq_rel`, the behavior
   * is undefined.
   *
   * \note `volatile` overloads are provided for compatibility with
   * `std::atomic`.
   */
  void store(const T& desired, const std::memory_order memory_order =
                                   std::memory_order_seq_cst) noexcept {
    const InternalData internal_desired =
        findus::detail::bit_cast<InternalData>(desired);
    load_store_impl<true>(data_, internal_desired, memory_order);
  }
  void store(const T& desired,
             const std::memory_order memory_order =
                 std::memory_order_seq_cst) volatile noexcept {
    const InternalData internal_desired =
        findus::detail::bit_cast<InternalData>(desired);
    load_store_impl<true>(data_, internal_desired, memory_order);
  }
  /// @}

  /// @{
  /*!
   * Atomically replaces the underlying value with desired (a
   * read-modify-write operation). Memory is affected according to the value of
   * `memory_order`.
   *
   * \note `volatile` overloads are provided for compatibility with
   * `std::atomic`.
   */
  T exchange(const T& desired, const std::memory_order memory_order =
                                   std::memory_order_seq_cst) noexcept {
    const InternalData internal_desired =
        findus::detail::bit_cast<InternalData>(desired);
    return exchange_impl(data_, internal_desired, memory_order);
  }
  T exchange(const T& desired,
             const std::memory_order memory_order =
                 std::memory_order_seq_cst) volatile noexcept {
    const InternalData internal_desired =
        findus::detail::bit_cast<InternalData>(desired);
    return exchange_impl(data_, internal_desired, memory_order);
  }
  /// @}

  /// @{
  /*!
   * \brief Atomically compares the value representation of `*this` with that of
   * `expected`. If those are bitwise-equal, replaces the former with `desired`
   * (performs read-modify-write operation). Otherwise, loads the actual value
   * stored in `*this` into `expected` (performs load operation).
   *
   * If `failure` is one of `std::memory_order_release` and
   * `std::memory_order_acq_rel`, the behavior is undefined.
   *
   * See the documentation of `std::atomic::compare_exchange_weak/strong` for
   * additional details.
   *
   * \note `volatile` overloads are provided for compatibility with
   * `std::atomic`.
   */
  bool compare_exchange_weak(T& expected, const T& desired,
                             const std::memory_order success,
                             const std::memory_order failure) noexcept {
    return compare_exchange_impl<false>(expected, data_, desired, success,
                                        failure);
  }
  bool compare_exchange_weak(
      T& expected, const T& desired, const std::memory_order success,
      const std::memory_order failure) volatile noexcept {
    return compare_exchange_impl<false>(expected, data_, desired, success,
                                        failure);
  }

  bool compare_exchange_weak(
      T& expected, const T& desired,
      const std::memory_order order = std::memory_order_seq_cst) noexcept {
    return compare_exchange_weak(expected, desired, order,
                                 select_cas_failure_order(order));
  }
  bool compare_exchange_weak(T& expected, const T& desired,
                             const std::memory_order order =
                                 std::memory_order_seq_cst) volatile noexcept {
    return compare_exchange_weak(expected, desired, order,
                                 select_cas_failure_order(order));
  }

  bool compare_exchange_strong(T& expected, const T& desired,
                               const std::memory_order success,
                               const std::memory_order failure) noexcept {
    return compare_exchange_impl<true>(expected, data_, desired, success,
                                       failure);
  }
  bool compare_exchange_strong(
      T& expected, const T& desired, const std::memory_order success,
      const std::memory_order failure) volatile noexcept {
    return compare_exchange_impl<true>(expected, data_, desired, success,
                                       failure);
  }

  bool compare_exchange_strong(
      T& expected, const T& desired,
      const std::memory_order order = std::memory_order_seq_cst) noexcept {
    return compare_exchange_strong(expected, desired, order,
                                   select_cas_failure_order(order));
  }
  bool compare_exchange_strong(
      T& expected, const T& desired,
      const std::memory_order order =
          std::memory_order_seq_cst) volatile noexcept {
    return compare_exchange_strong(expected, desired, order,
                                   select_cas_failure_order(order));
  }
  /// @}

 private:
  template <bool Store, class Destination, class Source>
#if defined(__x86_64__) || defined(_M_X64)
  // On x86-64 the sanitizers choke on the vmovdqa assembly
  // instructions. While these are atomic in hardware and so can properly be
  // used this way, the sanitizers seem to interfere with them. For example,
  // thread sanitizer incorrectly flags the vmovdqa as not thread safe.
  __attribute__((no_sanitize("thread", "address")))
#endif
  static void
  load_store_impl(Destination& destination, const Source& source,
                  const std::memory_order memory_order) noexcept {
    static_assert(std::is_same_v<std::remove_cv_t<Destination>, InternalData>);
    static_assert(std::is_same_v<std::remove_cv_t<Source>, InternalData>);
#if defined(__x86_64__) || defined(_M_X64)
    // We support both compile-time and run-time detection of AVX.
    // We need AVX support for the hardware to guarantee that vmovdqa is
    // atomic, in which case we can use that as the fast-path for load/store
    // operations. Operations that require sequential consistency still need
    // to use `lock cmpxchg16b`.
    if (const bool avx_supported =
#if defined(__AVX__)
            true;
#else
            findus::detail::x86_cpu_supports_16_byte_atomic();
#endif
        avx_supported and memory_order == std::memory_order_relaxed) {
      // Atomically load `source` into `destination` with
      // memory_order_relaxed. The compiler and the CPU are allowed to reorder
      // instructions other than those that depend on the load.
      __asm__ volatile("vmovdqa %[src], %[dst]"
                       : [dst] "=x"(destination)
                       : [src] "m"(source));
    } else if (avx_supported and
               (Store ? memory_order == std::memory_order_release
                      : memory_order == std::memory_order_acquire)) {
      // Atomically load `source` into `destination` with
      // memory_order_relaxed. The compiler and the CPU are not allowed to
      // reorder instructions. We use to clobber memory to prevent the compiler
      // from moving instructions from before/after this instruction.
      __asm__ volatile("vmovdqa %[src], %[dst]"
                       : [dst] "=x"(destination)
                       : [src] "m"(source)
                       : "memory");
    } else {
      // Atomically load/store a 128-bit value using cmpxchg16b to ensure
      // sequential consistent access, or if we don't support AVX vmovqda.
      if constexpr (Store) {
        // This assembly block:
        //   - Executes the `lock cmpxchg16b [dest_data]` instruction, which
        //     atomically compares the value in RDX:RAX with [dest_data]. If
        //     they match, [dest_data] is replaced with the value in RCX:RBX.
        //     Regardless of the result, the original value of [dest_data] is
        //     loaded into RDX:RAX.
        //   - In this context, the instruction is used to atomically store a
        //     128-bit value into [dest_data] by setting both the expected and
        //     desired values to the same value, ensuring the store always
        //     succeeds.
        //   - The `lock` prefix ensures the operation is performed atomically
        //     across all CPU cores, providing sequentially consistent ordering.
        //
        // Output:
        //   - [dest_data] is updated atomically with the new 128-bit value.
        //
        // Notes:
        //   - This ensures atomicity for 128-bit store operations on supported
        //     hardware.
        //   - Clobbers are listed for condition codes ("cc"), memory, and
        //     registers modified.
        //   - The explicit register usage ensures portability across compilers.
        __asm__ __volatile__("lock cmpxchg16b %[dest_data]"
                             : [dest_data] "+m"(destination)
                             : "a"(destination.low_bits),
                               "d"(destination.high_bits), "b"(source.low_bits),
                               "c"(source.high_bits)
                             : "memory", "cc");
      } else {
        // This assembly block:
        //   1. Zeroes out the ECX, EAX, EDX, and EBX registers.
        //   2. Executes the `lock cmpxchg16b [source]` instruction,
        //      which atomically compares EDX:EAX with [source] and,
        //      if they match, replaces [source] with ECX:EBX.
        //      The original value from [source] is always loaded into
        //      EDX:EAX. The `lock` synchronizes the entire cacheline across all
        //      CPU cores, ensure sequentially consistent ordering.
        //
        // Output:
        //   - destination.key receives the lower 64 bits (EAX)
        //   - destination.value receives the upper 64 bits (EDX)
        //
        // Notes:
        //   - This ensures atomicity for 128-bit operations on supported
        //     hardware.
        //   - Clobbers are listed for condition codes ("cc"), registers
        //   modified,
        //     and memory side effects.
        //   - GCC's `"A"` constraint shorthand works only in GCC and is not
        //     portable to Clang, so we always use the more verbose explicit
        //     syntax with explicit register zeroing.
        std::uint64_t eax = 0, edx = 0, ebx = 0, ecx = 0;
        __asm__ __volatile__(
            "lock cmpxchg16b %[src]"
            : "=a"(destination.low_bits), "=d"(destination.high_bits)
            : [src] "m"(source), "a"(eax), "d"(edx), "b"(ebx), "c"(ecx)
            : "cc", "memory");
      }
    }
#elif defined(__aarch64__) || defined(_M_ARM64)

    if constexpr (Store) {
      if (memory_order == std::memory_order_seq_cst) {
#if defined(FINDUS_ARM_LSE128)
        std::uint64_t old_low, old_high;
        __asm__ __volatile__(
            "swppal %[old_low], %[old_high], %[new_low], %[new_high], "
            "[%[ptr]]\n\t"
            : [old_low] "=&r"(old_low), [old_high] "=&r"(old_high)
            : [ptr] "r"(&destination), [new_low] "r"(source.low_bits),
              [new_high] "r"(source.high_bits)
            : "memory");
#elif defined(FINDUS_ARM_LSE2) and defined(FINDUS_ARM_RCPC3)
        __asm__ __volatile__(
            "stilp  %[low], %[high], [%[ptr]]\n\t"
            "dmb    ish\n\t"
            :
            : [ptr] "r"(&destination), [low] "r"(source.low_bits),
              [high] "r"(source.high_bits)
            : "memory");
#elif defined(FINDUS_ARM_LSE2)
        __asm__ __volatile__(
            "dmb    ish\n\t"
            "stp    %[low], %[high], [%[ptr]]\n\t"
            "dmb    ish\n\t"
            :
            : [ptr] "r"(&destination), [low] "r"(source.low_bits),
              [high] "r"(source.high_bits)
            : "memory");
#elif defined(FINDUS_ARM_LSE) and (not defined(__APPLE__))
        std::uint64_t old_low, old_high, tmp_low, tmp_high;
        __asm__ __volatile__(
            "ldp    x2, x3, [%[ptr]]\n\t"
            "1:\n\t"
            "mov    %[tmp_low], x2\n\t"
            "mov    %[tmp_high], x3\n\t"
            "mov    x4, %[new_low]\n\t"
            "mov    x5, %[new_high]\n\t"
            "caspal x2, x3, x4, x5, [%[ptr]]\n\t"
            "cmp    %[tmp_high], x3\n\t"
            "ccmp   %[tmp_low], x2, #0, eq\n\t"
            "b.ne   1b\n\t"
            "mov    %[old_low], x2\n\t"
            "mov    %[old_high], x3\n\t"
            : [old_low] "=&r"(old_low), [old_high] "=&r"(old_high),
              [tmp_low] "=&r"(tmp_low), [tmp_high] "=&r"(tmp_high)
            : [ptr] "r"(&destination), [new_low] "r"(source.low_bits),
              [new_high] "r"(source.high_bits)
            : "memory", "cc", "x2", "x3", "x4", "x5");
#else
        std::uint64_t tmp;
        __asm__ __volatile__(
            "1:\n\t"
            "ldaxp  xzr, %[tmp], [%[ptr]]\n\t"
            "stlxp  %w[tmp], %[low], %[high], [%[ptr]]\n\t"
            "cbnz   %w[tmp], 1b\n\t"
            : [tmp] "=&r"(tmp)
            : [ptr] "r"(&destination), [low] "r"(source.low_bits),
              [high] "r"(source.high_bits)
            : "memory");
#endif
      } else if (memory_order == std::memory_order_release) {
#if defined(FINDUS_ARM_LSE128)
        std::uint64_t old_low, old_high;

        __asm__ __volatile__(
            "swppl  %[old_low], %[old_high], %[new_low], %[new_high], "
            "[%[ptr]]\n\t"
            : [old_low] "=&r"(old_low), [old_high] "=&r"(old_high)
            : [ptr] "r"(&destination), [new_low] "r"(source.low_bits),
              [new_high] "r"(source.high_bits)
            : "memory");
#elif defined(FINDUS_ARM_RCPC3)
        __asm__ __volatile__(
            "stilp  %[low], %[high], [%[ptr]]\n\t"
            :
            : [ptr] "r"(&destination), [low] "r"(source.low_bits),
              [high] "r"(source.high_bits)
            : "memory");
#elif defined(FINDUS_ARM_LSE2)
        __asm__ __volatile__(
            "dmb    ish\n\t"
            "stp    %[low], %[high], [%[ptr]]\n\t"
            :
            : [ptr] "r"(&destination), [low] "r"(source.low_bits),
              [high] "r"(source.high_bits)
            : "memory");
#elif defined(FINDUS_ARM_LSE) and (not defined(__APPLE__))
        std::uint64_t old_low, old_high, tmp_low, tmp_high;

        __asm__ __volatile__(
            "ldp    x2, x3, [%[ptr]]\n\t"
            "1:\n\t"
            "mov    %[tmp_low], x2\n\t"
            "mov    %[tmp_high], x3\n\t"
            "mov    x4, %[new_low]\n\t"
            "mov    x5, %[new_high]\n\t"
            "caspl x2, x3, x4, x5, [%[ptr]]\n\t"
            "cmp    %[tmp_high], x3\n\t"
            "ccmp   %[tmp_low], x2, #0, eq\n\t"
            "b.ne   1b\n\t"
            "mov    %[old_low], x2\n\t"
            "mov    %[old_high], x3\n\t"
            : [old_low] "=&r"(old_low), [old_high] "=&r"(old_high),
              [tmp_low] "=&r"(tmp_low), [tmp_high] "=&r"(tmp_high)
            : [ptr] "r"(&destination), [new_low] "r"(source.low_bits),
              [new_high] "r"(source.high_bits)
            : "memory", "cc", "x2", "x3", "x4", "x5");
#else
        std::uint64_t tmp;

        __asm__ __volatile__(
            "1:\n\t"
            "ldxp   xzr, %[tmp], [%[ptr]]\n\t"
            "stlxp  %w[tmp], %[low], %[high], [%[ptr]]\n\t"
            "cbnz   %w[tmp], 1b\n\t"
            : [tmp] "=&r"(tmp)
            : [ptr] "r"(&destination), [low] "r"(source.low_bits),
              [high] "r"(source.high_bits)
            : "memory");
#endif
      } else {
        // memory_order_relaxed
#if defined(FINDUS_ARM_RCPC3) or defined(FINDUS_ARM_LSE2)
        __asm__ __volatile__(
            "stp  %[low], %[high], [%[ptr]]\n\t"
            :
            : [ptr] "r"(&destination), [low] "r"(source.low_bits),
              [high] "r"(source.high_bits));
#elif defined(FINDUS_ARM_LSE) and (not defined(__APPLE__))
        std::uint64_t old_low, old_high, tmp_low, tmp_high;

        __asm__ __volatile__(
            "ldp    x2, x3, [%[ptr]]\n\t"
            "1:\n\t"
            "mov    %[tmp_low], x2\n\t"
            "mov    %[tmp_high], x3\n\t"
            "mov    x4, %[new_low]\n\t"
            "mov    x5, %[new_high]\n\t"
            "caspl x2, x3, x4, x5, [%[ptr]]\n\t"
            "cmp    %[tmp_high], x3\n\t"
            "ccmp   %[tmp_low], x2, #0, eq\n\t"
            "b.ne   1b\n\t"
            "mov    %[old_low], x2\n\t"
            "mov    %[old_high], x3\n\t"
            : [old_low] "=&r"(old_low), [old_high] "=&r"(old_high),
              [tmp_low] "=&r"(tmp_low), [tmp_high] "=&r"(tmp_high)
            : [ptr] "r"(&destination), [new_low] "r"(source.low_bits),
              [new_high] "r"(source.high_bits)
            : "cc", "x2", "x3", "x4", "x5");
#else
        std::uint64_t tmp;

        __asm__ __volatile__(
            "1:\n\t"
            "ldxp   xzr, %[tmp], [%[ptr]]\n\t"
            "stlxp  %w[tmp], %[low], %[high], [%[ptr]]\n\t"
            "cbnz   %w[tmp], 1b\n\t"
            : [tmp] "=&r"(tmp)
            : [ptr] "r"(&destination), [low] "r"(source.low_bits),
              [high] "r"(source.high_bits)
            : "memory");
#endif
      }
    } else { // if constexpr (Store), i.e. we are doing a load.
      if (memory_order == std::memory_order_seq_cst) {
#if defined(FINDUS_ARM_RCPC3)
        __asm__ __volatile__(
            "ldar   %[low], [%[ptr]]\n\t"
            "ldiapp %[low], %[high], [%[ptr]]\n\t"
            : [low] "=&r"(destination.low_bits), [high] "=r"(
                                                     destination.high_bits)
            : [ptr] "r"(&source)
            : "memory");
#elif defined(FINDUS_ARM_LSE128) or defined(FINDUS_ARM_LSE2)
        __asm__ __volatile__(
            "ldar   %[low], [%[ptr]]\n\t"
            "ldp    %[low], %[high], [%[ptr]]\n\t"
            "dmb    ishld\n\t"
            : [low] "=&r"(destination.low_bits), [high] "=r"(
                                                     destination.high_bits)
            : [ptr] "r"(&source)
            : "memory");
#elif defined(FINDUS_ARM_LSE) and (not defined(__APPLE__))
        // on Apple hardware, caspal is slower than ldxp/stxp.
        destination.low_bits = 0;
        destination.high_bits = 0;
        __asm__ __volatile__(
            "mov    x2, xzr\n\t"
            "mov    x3, xzr\n\t"
            "caspal x2, x3, x2, x3, [%[ptr]]\n\t"
            "mov    %[low], x2\n\t"
            "mov    %[high], x3\n\t"
            : [low] "=r"(destination.low_bits), [high] "=r"(
                                                    destination.high_bits)
            : [ptr] "r"(&source)
            : "memory", "x2", "x3");
#else
        std::uint64_t tmp;
        __asm__ __volatile__(
            "1:\n\t"
            "ldaxp  %[low], %[high], [%[ptr]]\n\t"
            "stlxp  %w[tmp], %[low], %[high], [%[ptr]]\n\t"
            "cbnz   %w[tmp], 1b\n\t"
            : [low] "=&r"(destination.low_bits),
              [high] "=&r"(destination.high_bits), [tmp] "=&r"(tmp)
            : [ptr] "r"(&source)
            : "memory");
#endif
      } else if (memory_order == std::memory_order_acquire) {
#if defined(FINDUS_ARM_RCPC3)
        __asm__ __volatile__(
            "ldiapp %[low], %[high], [%[ptr]]\n\t"
            : [low] "=&r"(destination.low_bits), [high] "=r"(
                                                     destination.high_bits)
            : [ptr] "r"(&source)
            : "memory");
#elif defined(FINDUS_ARM_LSE128) or defined(FINDUS_ARM_LSE2)
        __asm__ __volatile__(
            "ldp    %[low], %[high], [%[ptr]]\n\t"
            "dmb    ishld\n\t"
            : [low] "=&r"(destination.low_bits), [high] "=r"(
                                                     destination.high_bits)
            : [ptr] "r"(&source)
            : "memory");
#elif defined(FINDUS_ARM_LSE) and (not defined(__APPLE__))
        // on Apple hardware, caspal is slower than ldxp/stxp.
        destination.low_bits = 0;
        destination.high_bits = 0;
        __asm__ __volatile__(
            "mov    x2, xzr\n\t"
            "mov    x3, xzr\n\t"
            "caspa  x2, x3, x2, x3, [%[ptr]]\n\t"
            "mov    %[low], x2\n\t"
            "mov    %[high], x3\n\t"
            : [low] "=r"(destination.low_bits), [high] "=r"(
                                                    destination.high_bits)
            : [ptr] "r"(&source)
            : "memory", "x2", "x3");
#else
        std::uint64_t tmp;
        __asm__ __volatile__(
            "1:\n\t"
            "ldaxp  %[low], %[high], [%[ptr]]\n\t"
            "stxp  %w[tmp], %[low], %[high], [%[ptr]]\n\t"
            "cbnz   %w[tmp], 1b\n\t"
            : [low] "=&r"(destination.low_bits),
              [high] "=&r"(destination.high_bits), [tmp] "=&r"(tmp)
            : [ptr] "r"(&source)
            : "memory");
#endif
      } else {
        // std::memory_order_relaxed
#if defined(FINDUS_ARM_RCPC3) or defined(FINDUS_ARM_LSE128) or                 \
    defined(FINDUS_ARM_LSE2)
        __asm__ __volatile__(
            "ldp    %[low], %[high], [%[ptr]]\n\t"
            : [low] "=&r"(destination.low_bits), [high] "=r"(
                                                     destination.high_bits)
            : [ptr] "r"(&source));
#elif defined(FINDUS_ARM_LSE) and (not defined(__APPLE__))
        destination.low_bits = 0;
        destination.high_bits = 0;
        __asm__ __volatile__(
            "mov    x2, xzr\n\t"
            "mov    x3, xzr\n\t"
            "casp  x2, x3, x2, x3, [%[ptr]]\n\t"
            "mov    %[low], x2\n\t"
            "mov    %[high], x3\n\t"
            : [low] "=r"(destination.low_bits), [high] "=r"(
                                                    destination.high_bits)
            : [ptr] "r"(&source)
            : "memory", "x2", "x3");
#else
        std::uint32_t tmp;
        __asm__ __volatile__(
            "1:\n\t"
            "ldxp  %[low], %[high], [%[ptr]]\n\t"
            "stxp  %w[tmp], %[low], %[high], [%[ptr]]\n\t"
            "cbnz   %w[tmp], 1b\n\t"
            : [low] "=&r"(destination.low_bits),
              [high] "=&r"(destination.high_bits), [tmp] "=&r"(tmp)
            : [ptr] "r"(&source));
#endif
      }
    }
#elif defined(__PPC64__) || defined(__ppc64__) || defined(_ARCH_PPC64)
#error "Unsupported architecture."
#else
#error "Unsupported architecture."
#endif
  }

  static std::memory_order select_cas_failure_order(
      const std::memory_order order) noexcept {
    if (order == std::memory_order_acq_rel) {
      return std::memory_order_acquire;
    }
    if (order == std::memory_order_release) {
      return std::memory_order_relaxed;
    }
    return order;
  }

  template <bool StrongExchange, class Destination>
  static bool compare_exchange_impl(T& expected, Destination& destination,
                                    const T& desired,
                                    const std::memory_order success,
                                    const std::memory_order failure) noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    InternalData internal_expected =
        findus::detail::bit_cast<InternalData>(expected);

    // If our failure memory order is not seq_cst, then we first try a load
    // operation and comparison. This is the best we can do on x86-64 since we
    // don't have support for LL/SC operations.
    if (failure == std::memory_order_acquire or
        failure == std::memory_order_relaxed) {
      InternalData current_value{};
      load_store_impl<false>(current_value, destination, failure);
      if (current_value != internal_expected) {
        std::memcpy(std::addressof(expected), std::addressof(current_value),
                    sizeof(T));
        return false;
      }
    }

    // We have to do seq_cst for the comxchg since nothing else (like LL/SC) is
    // support on x86 at the moment.
    (void)success;
    bool was_successful;
    // This assembly block:
    //   - Executes `lock cmpxchg16b [destination]`, which atomically
    //     compares RDX:RAX with [destination]. If they match, [destination]
    //     is replaced with RCX:RBX. The original value from [destination]
    //     is always loaded into RDX:RAX.
    //   - After the comparison, the `sete` instruction sets the
    //     `was_successful` variable to 1 if the exchange succeeded
    //     (Zero Flag set), or 0 otherwise.
    //   - The `lock` prefix ensures atomicity across all CPU cores,
    //     providing sequentially consistent ordering.
    //
    // Output:
    //   - `was_successful` is set to true if the exchange succeeded,
    //     false otherwise.
    //   - `expected.low_bits` and `expected.high_bits` are updated
    //     with the value from [destination] if the exchange failed.
    //
    // Notes:
    //   - Ensures atomicity for 128-bit compare-and-exchange on
    //     supported hardware.
    //   - Clobbers are listed for condition codes ("cc") and memory
    //     side effects.
    //   - The Zero Flag (ZF) is used to indicate success, and is
    //     captured by the `sete` instruction.
    const InternalData internal_desired =
        findus::detail::bit_cast<InternalData>(desired);
    __asm__ __volatile__(
        "lock cmpxchg16b %1\n\t"
        "sete %0"
        : "=q"(was_successful), "+m"(destination),
          "+a"(internal_expected.low_bits), "+d"(internal_expected.high_bits)
        : "b"(internal_desired.low_bits), "c"(internal_desired.high_bits)
        : "cc", "memory");

    std::memcpy(std::addressof(expected), std::addressof(internal_expected),
                sizeof(T));
    return was_successful;
#elif defined(__aarch64__) || defined(_M_ARM64)
    bool was_successful;
    InternalData internal_expected =
        findus::detail::bit_cast<InternalData>(expected);
    InternalData exp = internal_expected;
    const InternalData internal_desired =
        findus::detail::bit_cast<InternalData>(desired);
#if defined(FINDUS_ARM_LSE)
    if ((success == std::memory_order_seq_cst or
         success == std::memory_order_acq_rel) or
        failure == std::memory_order_seq_cst or
        (success == std::memory_order_release and
         failure == std::memory_order_acquire)) {
      __asm__ __volatile__(
          "mov    x2, %[old_low]\n\t"
          "mov    x3, %[old_high]\n\t"
          "mov    x4, %[new_low]\n\t"
          "mov    x5, %[new_high]\n\t"
          "caspal x2, x3, x4, x5, [%[ptr]]\n\t"
          "mov    %[old_low], x2\n\t"
          "mov    %[old_high], x3\n\t"
          "cmp    %[old_low], %[exp_low]\n\t"
          "ccmp   %[old_high], %[exp_high], #0, eq\n\t"
          "cset   %w[was_successful], eq\n\t"
          : [old_low] "+&r"(internal_expected.low_bits),
            [old_high] "+&r"(internal_expected.high_bits),
            [was_successful] "=r"(was_successful)
          : [ptr] "r"(&destination), [new_low] "r"(internal_desired.low_bits),
            [new_high] "r"(internal_desired.high_bits),
            [exp_low] "r"(exp.low_bits), [exp_high] "r"(exp.high_bits)
          : "memory", "cc", "x2", "x3", "x4", "x5");
    } else if (success == std::memory_order_acquire or
               failure == std::memory_order_acquire) {
      __asm__ __volatile__(
          "mov    x2, %[old_low]\n\t"
          "mov    x3, %[old_high]\n\t"
          "mov    x4, %[new_low]\n\t"
          "mov    x5, %[new_high]\n\t"
          "caspa x2, x3, x4, x5, [%[ptr]]\n\t"
          "mov    %[old_low], x2\n\t"
          "mov    %[old_high], x3\n\t"
          "cmp    %[old_low], %[exp_low]\n\t"
          "ccmp   %[old_high], %[exp_high], #0, eq\n\t"
          "cset   %w[was_successful], eq\n\t"
          : [old_low] "+&r"(internal_expected.low_bits),
            [old_high] "+&r"(internal_expected.high_bits),
            [was_successful] "=r"(was_successful)
          : [ptr] "r"(&destination), [new_low] "r"(internal_desired.low_bits),
            [new_high] "r"(internal_desired.high_bits),
            [exp_low] "r"(exp.low_bits), [exp_high] "r"(exp.high_bits)
          : "memory", "cc", "x2", "x3", "x4", "x5");
    } else if (success == std::memory_order_release) {
      __asm__ __volatile__(
          "mov    x2, %[old_low]\n\t"
          "mov    x3, %[old_high]\n\t"
          "mov    x4, %[new_low]\n\t"
          "mov    x5, %[new_high]\n\t"
          "caspl x2, x3, x4, x5, [%[ptr]]\n\t"
          "mov    %[old_low], x2\n\t"
          "mov    %[old_high], x3\n\t"
          "cmp    %[old_low], %[exp_low]\n\t"
          "ccmp   %[old_high], %[exp_high], #0, eq\n\t"
          "cset   %w[was_successful], eq\n\t"
          : [old_low] "+&r"(internal_expected.low_bits),
            [old_high] "+&r"(internal_expected.high_bits),
            [was_successful] "=r"(was_successful)
          : [ptr] "r"(&destination), [new_low] "r"(internal_desired.low_bits),
            [new_high] "r"(internal_desired.high_bits),
            [exp_low] "r"(exp.low_bits), [exp_high] "r"(exp.high_bits)
          : "memory", "cc", "x2", "x3", "x4", "x5");
    } else {
      __asm__ __volatile__(
          "mov    x2, %[old_low]\n\t"
          "mov    x3, %[old_high]\n\t"
          "mov    x4, %[new_low]\n\t"
          "mov    x5, %[new_high]\n\t"
          "casp x2, x3, x4, x5, [%[ptr]]\n\t"
          "mov    %[old_low], x2\n\t"
          "mov    %[old_high], x3\n\t"
          "cmp    %[old_low], %[exp_low]\n\t"
          "ccmp   %[old_high], %[exp_high], #0, eq\n\t"
          "cset   %w[was_successful], eq\n\t"
          : [old_low] "+&r"(internal_expected.low_bits),
            [old_high] "+&r"(internal_expected.high_bits),
            [was_successful] "=r"(was_successful)
          : [ptr] "r"(&destination), [new_low] "r"(internal_desired.low_bits),
            [new_high] "r"(internal_desired.high_bits),
            [exp_low] "r"(exp.low_bits), [exp_high] "r"(exp.high_bits)
          : "cc", "x2", "x3", "x4", "x5");
    }
#else
    std::uint32_t tmp;
    if ((success == std::memory_order_seq_cst or
         success == std::memory_order_acq_rel) or
        failure == std::memory_order_seq_cst or
        (success == std::memory_order_release and
         failure == std::memory_order_acquire)) {
      __asm__ __volatile__(
          "1:\n\t"
          "ldaxp  %[old_low], %[old_high], [%[ptr]]\n\t"
          "cmp    %[old_low], %[exp_low]\n\t"
          "cset   %w[tmp], ne\n\t"
          "cmp    %[old_high], %[exp_high]\n\t"
          "cinc   %w[tmp], %w[tmp], ne\n\t"
          "cbz    %w[tmp], 2f\n\t"
          "stlxp  %w[tmp], %[old_low], %[old_high], [%[ptr]]\n\t"
          "cbnz   %w[tmp], 1b\n\t"
          "b      3f\n\t"
          "2:\n\t"
          "stlxp  %w[tmp], %[new_low], %[new_high], [%[ptr]]\n\t"
          "cbnz   %w[tmp], 1b\n\t"
          "3:\n\t"
          "cmp    %[old_low], %[exp_low]\n\t"
          "ccmp   %[old_high], %[exp_high], #0, eq\n\t"
          "cset   %w[was_successful], eq\n\t"
          : [old_low] "+&r"(internal_expected.low_bits),
            [old_high] "+&r"(internal_expected.high_bits), [tmp] "=&r"(tmp),
            [was_successful] "=r"(was_successful)
          : [ptr] "r"(&destination), [exp_low] "r"(exp.low_bits),
            [exp_high] "r"(exp.high_bits),
            [new_low] "r"(internal_desired.low_bits),
            [new_high] "r"(internal_desired.high_bits)
          : "memory", "cc");
    } else if (success == std::memory_order_acquire or
               failure == std::memory_order_acquire) {
      __asm__ __volatile__(
          "1:\n\t"
          "ldaxp  %[old_low], %[old_high], [%[ptr]]\n\t"
          "cmp    %[old_low], %[exp_low]\n\t"
          "cset   %w[tmp], ne\n\t"
          "cmp    %[old_high], %[exp_high]\n\t"
          "cinc   %w[tmp], %w[tmp], ne\n\t"
          "cbz    %w[tmp], 2f\n\t"
          "stxp  %w[tmp], %[old_low], %[old_high], [%[ptr]]\n\t"
          "cbnz   %w[tmp], 1b\n\t"
          "b      3f\n\t"
          "2:\n\t"
          "stlxp  %w[tmp], %[new_low], %[new_high], [%[ptr]]\n\t"
          "cbnz   %w[tmp], 1b\n\t"
          "3:\n\t"
          "cmp    %[old_low], %[exp_low]\n\t"
          "ccmp   %[old_high], %[exp_high], #0, eq\n\t"
          "cset   %w[was_successful], eq\n\t"
          : [old_low] "+&r"(internal_expected.low_bits),
            [old_high] "+&r"(internal_expected.high_bits), [tmp] "=&r"(tmp),
            [was_successful] "=r"(was_successful)
          : [ptr] "r"(&destination), [exp_low] "r"(exp.low_bits),
            [exp_high] "r"(exp.high_bits),
            [new_low] "r"(internal_desired.low_bits),
            [new_high] "r"(internal_desired.high_bits)
          : "memory", "cc");
    } else if (success == std::memory_order_release) {
      __asm__ __volatile__(
          "1:\n\t"
          "ldxp  %[old_low], %[old_high], [%[ptr]]\n\t"
          "cmp    %[old_low], %[exp_low]\n\t"
          "cset   %w[tmp], ne\n\t"
          "cmp    %[old_high], %[exp_high]\n\t"
          "cinc   %w[tmp], %w[tmp], ne\n\t"
          "cbz    %w[tmp], 2f\n\t"
          "stlxp  %w[tmp], %[old_low], %[old_high], [%[ptr]]\n\t"
          "cbnz   %w[tmp], 1b\n\t"
          "b      3f\n\t"
          "2:\n\t"
          "stlxp  %w[tmp], %[new_low], %[new_high], [%[ptr]]\n\t"
          "cbnz   %w[tmp], 1b\n\t"
          "3:\n\t"
          "cmp    %[old_low], %[exp_low]\n\t"
          "ccmp   %[old_high], %[exp_high], #0, eq\n\t"
          "cset   %w[was_successful], eq\n\t"
          : [old_low] "+&r"(internal_expected.low_bits),
            [old_high] "+&r"(internal_expected.high_bits), [tmp] "=&r"(tmp),
            [was_successful] "=r"(was_successful)
          : [ptr] "r"(&destination), [exp_low] "r"(exp.low_bits),
            [exp_high] "r"(exp.high_bits),
            [new_low] "r"(internal_desired.low_bits),
            [new_high] "r"(internal_desired.high_bits)
          : "memory", "cc");
    } else {
      __asm__ __volatile__(
          "1:\n\t"
          "ldxp  %[old_low], %[old_high], [%[ptr]]\n\t"
          "cmp    %[old_low], %[exp_low]\n\t"
          "cset   %w[tmp], ne\n\t"
          "cmp    %[old_high], %[exp_high]\n\t"
          "cinc   %w[tmp], %w[tmp], ne\n\t"
          "cbz    %w[tmp], 2f\n\t"
          "stlxp  %w[tmp], %[old_low], %[old_high], [%[ptr]]\n\t"
          "cbnz   %w[tmp], 1b\n\t"
          "b      3f\n\t"
          "2:\n\t"
          "stxp  %w[tmp], %[new_low], %[new_high], [%[ptr]]\n\t"
          "cbnz   %w[tmp], 1b\n\t"
          "3:\n\t"
          "cmp    %[old_low], %[exp_low]\n\t"
          "ccmp   %[old_high], %[exp_high], #0, eq\n\t"
          "cset   %w[was_successful], eq\n\t"
          : [old_low] "+&r"(internal_expected.low_bits),
            [old_high] "+&r"(internal_expected.high_bits), [tmp] "=&r"(tmp),
            [was_successful] "=r"(was_successful)
          : [ptr] "r"(&destination), [exp_low] "r"(exp.low_bits),
            [exp_high] "r"(exp.high_bits),
            [new_low] "r"(internal_desired.low_bits),
            [new_high] "r"(internal_desired.high_bits)
          : "memory", "cc");
    }
#endif
    expected = detail::bit_cast<T>(internal_expected);
    return was_successful;
#elif defined(__PPC64__) || defined(__ppc64__) || defined(_ARCH_PPC64)
#error "Unsupported architecture."
#else
#error "Unsupported architecture."
#endif
  }

  template <class Destination>
  static T exchange_impl(Destination& destination, const InternalData& desired,
                         const std::memory_order order) noexcept {
    static_assert(std::is_same_v<std::remove_cv_t<Destination>, InternalData>);
    InternalData result_data{};
#if defined(__x86_64__) || defined(_M_X64)
    // This assembly block:
    //   - Executes `lock cmpxchg16b [dest_data]`, which atomically
    //     compares RDX:RAX with [dest_data]. If they match, [dest_data]
    //     is replaced with RCX:RBX. The original value of [dest_data]
    //     is always loaded into RDX:RAX. Since the compared values always
    //     match (they point to the same data), this is an exchange operation.
    //   - The `lock` prefix ensures atomicity across all CPU cores,
    //     providing sequentially consistent ordering.
    //
    // Output:
    //   - `result_data.low_bits` receives the lower 64 bits (RAX) of
    //     the original value from [dest_data].
    //   - `result_data.high_bits` receives the upper 64 bits (RDX) of
    //     the original value from [dest_data].
    //
    // Notes:
    //   - Ensures atomicity for 128-bit exchange operations on
    //     supported hardware.
    //   - Clobbers are listed for condition codes ("cc") and memory
    //     side effects when memory ordering stronger than relaxed is requested.
    if (order == std::memory_order_relaxed) {
      __asm__ __volatile__("lock cmpxchg16b %[dest_data]"
                           : [dest_data] "+m"(destination),
                             [result_data] "=a"(result_data.low_bits),
                             "=d"(result_data.high_bits)
                           : "a"(destination.low_bits),
                             "d"(destination.high_bits), "b"(desired.low_bits),
                             "c"(desired.high_bits)
                           : "cc");
    } else {
      __asm__ __volatile__("lock cmpxchg16b %[dest_data]"
                           : [dest_data] "+m"(destination),
                             [result_data] "=a"(result_data.low_bits),
                             "=d"(result_data.high_bits)
                           : "a"(destination.low_bits),
                             "d"(destination.high_bits), "b"(desired.low_bits),
                             "c"(desired.high_bits)
                           : "memory", "cc");
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    if (order == std::memory_order_seq_cst or
        order == std::memory_order_acq_rel) {
#if defined(FINDUS_ARM_LSE128)
      result_data = desired;
      __asm__ __volatile__(
          "swppal %[low], %[high], [%[ptr]]\n\t"
          : [low] "+r"(result_data.low_bits), [high] "+r"(result_data.high_bits)
          : [ptr] "r"(&destination)
          : "memory");
#elif defined(FINDUS_ARM_LSE) and (not defined(__APPLE__))
      std::uint64_t tmp_low, tmp_high;

      __asm__ __volatile__(
          "ldp    x2, x3, [%[ptr]]\n\t"
          "mov    x4, %[new_low]\n\t"
          "mov    x5, %[new_high]\n\t"
          "1:\n\t"
          "mov    %[tmp_low], x2\n\t"
          "mov    %[tmp_high], x3\n\t"
          "caspal x2, x3, x4, x5, [%[ptr]]\n\t"
          "cmp    %[tmp_high], x3\n\t"
          "ccmp   %[tmp_low], x2, #0, eq\n\t"
          "b.ne   1b\n\t"
          "mov    %[old_low], x2\n\t"
          "mov    %[old_high], x3\n\t"
          : [old_low] "=&r"(result_data.low_bits),
            [old_high] "=&r"(result_data.high_bits), [tmp_low] "=&r"(tmp_low),
            [tmp_high] "=&r"(tmp_high)
          : [ptr] "r"(&destination), [new_low] "r"(desired.low_bits),
            [new_high] "r"(desired.high_bits)
          : "memory", "cc", "x2", "x3", "x4", "x5");
#else
      std::uint32_t tmp;
      __asm__ __volatile__(
          "1:\n\t"
          "ldaxp  %[old_low], %[old_high], [%[ptr]]\n\t"
          "stlxp  %w[tmp], %[new_low], %[new_high], [%[ptr]]\n\t"
          "cbnz   %w[tmp], 1b\n\t"
          : [old_low] "=&r"(result_data.low_bits),
            [old_high] "=&r"(result_data.high_bits), [tmp] "=&r"(tmp)
          : [ptr] "r"(&destination), [new_low] "r"(desired.low_bits),
            [new_high] "r"(desired.high_bits)
          : "memory");
#endif
    } else if (order == std::memory_order_acquire) {
#if defined(FINDUS_ARM_LSE128)
      result_data = desired;
      __asm__ __volatile__(
          "swppa %[low], %[high], [%[ptr]]\n\t"
          : [low] "+r"(result_data.low_bits), [high] "+r"(result_data.high_bits)
          : [ptr] "r"(&destination)
          : "memory");
#elif defined(FINDUS_ARM_LSE) and (not defined(__APPLE__))
      std::uint64_t tmp_low, tmp_high;

      __asm__ __volatile__(
          "ldp    x2, x3, [%[ptr]]\n\t"
          "mov    x4, %[new_low]\n\t"
          "mov    x5, %[new_high]\n\t"
          "1:\n\t"
          "mov    %[tmp_low], x2\n\t"
          "mov    %[tmp_high], x3\n\t"
          "caspa x2, x3, x4, x5, [%[ptr]]\n\t"
          "cmp    %[tmp_high], x3\n\t"
          "ccmp   %[tmp_low], x2, #0, eq\n\t"
          "b.ne   1b\n\t"
          "mov    %[old_low], x2\n\t"
          "mov    %[old_high], x3\n\t"
          : [old_low] "=&r"(result_data.low_bits),
            [old_high] "=&r"(result_data.high_bits), [tmp_low] "=&r"(tmp_low),
            [tmp_high] "=&r"(tmp_high)
          : [ptr] "r"(&destination), [new_low] "r"(desired.low_bits),
            [new_high] "r"(desired.high_bits)
          : "memory", "cc", "x2", "x3", "x4", "x5");
#else
      std::uint32_t tmp;
      __asm__ __volatile__(
          "1:\n\t"
          "ldaxp  %[old_low], %[old_high], [%[ptr]]\n\t"
          "stxp  %w[tmp], %[new_low], %[new_high], [%[ptr]]\n\t"
          "cbnz   %w[tmp], 1b\n\t"
          : [old_low] "=&r"(result_data.low_bits),
            [old_high] "=&r"(result_data.high_bits), [tmp] "=&r"(tmp)
          : [ptr] "r"(&destination), [new_low] "r"(desired.low_bits),
            [new_high] "r"(desired.high_bits)
          : "memory");
#endif
    }
    if (order == std::memory_order_release) {
#if defined(FINDUS_ARM_LSE128)
      result_data = desired;
      __asm__ __volatile__(
          "swppl %[low], %[high], [%[ptr]]\n\t"
          : [low] "+r"(result_data.low_bits), [high] "+r"(result_data.high_bits)
          : [ptr] "r"(&destination)
          : "memory");
#elif defined(FINDUS_ARM_LSE) and (not defined(__APPLE__))
      std::uint64_t tmp_low, tmp_high;

      __asm__ __volatile__(
          "ldp    x2, x3, [%[ptr]]\n\t"
          "mov    x4, %[new_low]\n\t"
          "mov    x5, %[new_high]\n\t"
          "1:\n\t"
          "mov    %[tmp_low], x2\n\t"
          "mov    %[tmp_high], x3\n\t"
          "caspl x2, x3, x4, x5, [%[ptr]]\n\t"
          "cmp    %[tmp_high], x3\n\t"
          "ccmp   %[tmp_low], x2, #0, eq\n\t"
          "b.ne   1b\n\t"
          "mov    %[old_low], x2\n\t"
          "mov    %[old_high], x3\n\t"
          : [old_low] "=&r"(result_data.low_bits),
            [old_high] "=&r"(result_data.high_bits), [tmp_low] "=&r"(tmp_low),
            [tmp_high] "=&r"(tmp_high)
          : [ptr] "r"(&destination), [new_low] "r"(desired.low_bits),
            [new_high] "r"(desired.high_bits)
          : "memory", "cc", "x2", "x3", "x4", "x5");
#else
      std::uint32_t tmp;
      __asm__ __volatile__(
          "1:\n\t"
          "ldxp  %[old_low], %[old_high], [%[ptr]]\n\t"
          "stlxp  %w[tmp], %[new_low], %[new_high], [%[ptr]]\n\t"
          "cbnz   %w[tmp], 1b\n\t"
          : [old_low] "=&r"(result_data.low_bits),
            [old_high] "=&r"(result_data.high_bits), [tmp] "=&r"(tmp)
          : [ptr] "r"(&destination), [new_low] "r"(desired.low_bits),
            [new_high] "r"(desired.high_bits)
          : "memory");
#endif
    }
    if (order == std::memory_order_relaxed) {
#if defined(FINDUS_ARM_LSE128)
      result_data = desired;
      __asm__ __volatile__(
          "swpp %[low], %[high], [%[ptr]]\n\t"
          : [low] "+r"(result_data.low_bits), [high] "+r"(result_data.high_bits)
          : [ptr] "r"(&destination));
#elif defined(FINDUS_ARM_LSE) and (not defined(__APPLE__))
      std::uint64_t tmp_low, tmp_high;

      __asm__ __volatile__(
          "ldp    x2, x3, [%[ptr]]\n\t"
          "mov    x4, %[new_low]\n\t"
          "mov    x5, %[new_high]\n\t"
          "1:\n\t"
          "mov    %[tmp_low], x2\n\t"
          "mov    %[tmp_high], x3\n\t"
          "casp x2, x3, x4, x5, [%[ptr]]\n\t"
          "cmp    %[tmp_high], x3\n\t"
          "ccmp   %[tmp_low], x2, #0, eq\n\t"
          "b.ne   1b\n\t"
          "mov    %[old_low], x2\n\t"
          "mov    %[old_high], x3\n\t"
          : [old_low] "=&r"(result_data.low_bits),
            [old_high] "=&r"(result_data.high_bits), [tmp_low] "=&r"(tmp_low),
            [tmp_high] "=&r"(tmp_high)
          : [ptr] "r"(&destination), [new_low] "r"(desired.low_bits),
            [new_high] "r"(desired.high_bits)
          : "cc", "x2", "x3", "x4", "x5");
#else
      std::uint32_t tmp;
      __asm__ __volatile__(
          "1:\n\t"
          "ldxp  %[old_low], %[old_high], [%[ptr]]\n\t"
          "stxp  %w[tmp], %[new_low], %[new_high], [%[ptr]]\n\t"
          "cbnz   %w[tmp], 1b\n\t"
          : [old_low] "=&r"(result_data.low_bits),
            [old_high] "=&r"(result_data.high_bits), [tmp] "=&r"(tmp)
          : [ptr] "r"(&destination), [new_low] "r"(desired.low_bits),
            [new_high] "r"(desired.high_bits));
#endif
    }
#elif defined(__PPC64__) || defined(__ppc64__) || defined(_ARCH_PPC64)
#error "Unsupported architecture."
#else
#error "Unsupported architecture."
#endif
    return findus::detail::bit_cast<T>(result_data);
  }

  InternalData data_{};
};
}  // namespace findus
