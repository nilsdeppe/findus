// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "findus/Atomic128.hpp"

#include <type_traits>

#if defined(FINDUS_ENABLE_TESTING)

#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <doctest/doctest.h>
#include <ios>
#include <iostream>
#include <thread>
#include <vector>

#include "findus/HardwareInfo.hpp"

namespace findus {
namespace {
/*!
 * Pattern: 128-bit payload with utility functions.
 * Up to 8 bit patterns are used for atomic stress test.
 *
 * If a read returns a value other than these, a torn (non-atomic) operation has
 * occurred.
 */
struct alignas(16) Pattern {
  uint64_t hi, lo;
  static constexpr std::array<std::uint64_t, 14> patterns{
      0xAAAAAAAAAAAAAAAAULL,  // already used
      0x5555555555555555ULL,  // already used
      0x0000000000000000ULL,  // all zeroes
      0xFFFFFFFFFFFFFFFFULL,  // all ones
      0xF0F0F0F0F0F0F0F0ULL,  // alternating 11110000
      0x0F0F0F0F0F0F0F0FULL,  // alternating 00001111
      0x3333333333333333ULL,  // alternating 00110011
      0xCCCCCCCCCCCCCCCCULL,  // alternating 11001100
      0xA5A5A5A5A5A5A5A5ULL,  // 10100101...
      0x5A5A5A5A5A5A5A5AULL,  // 01011010...
      0x9696969696969696ULL,  // 10010110...
      0x6969696969696969ULL,  // 01101001...
      0xFF00FF00FF00FF00ULL,  // 1111111100000000...
      0x00FF00FF00FF00FFULL   // 0000000011111111...
  };

  // Return true if this is the ith pattern
  bool is_pattern(const std::uint64_t index) const {
    return hi == patterns[index] and lo == patterns[index];
  }

  // Return true if this is either allowed pattern
  bool is_valid() const {
    for (size_t i = 0; i < patterns.size(); ++i) {
      if (is_pattern(i)) {
        return true;
      }
    }
    return false;
  }
};

inline void set_pattern(Pattern& x, const std::uint64_t index) {
  x.hi = x.lo = Pattern::patterns[index];
}

enum class Check { Uninitialized, LoadStore, CasWeak, CasStrong, Exchange };

/*
 * Writer thread: repeatedly stores a full 128-bit pattern into the atomic.
 * Args:
 *   atomic: reference to the Atomic128 instance
 *   pat: pattern to write (A or B)
 *   order: memory order
 */
template <int NumberOfIterations, class T>
void store_thread(T& atomic, const Pattern& pat,
                  const std::memory_order order) {
  static_assert(std::is_same_v<std::remove_cv_t<T>, Atomic128<Pattern>>);
  for (int i = 0; i < NumberOfIterations; ++i) {
    atomic.store(pat, order);
  }
}

/*
 * Reader thread: repeatedly loads the value and checks it.
 * Sets error=true if any loaded value is not one of the allowed patterns.
 * Args:
 *   atomic: reference to the Atomic128 instance
 *   error: atomic flag set to true on error
 *   order: memory order
 */
template <int NumberOfIterations, class T>
void load_thread(T& atomic, std::atomic<bool>& error,
                 const std::memory_order order) {
  static_assert(std::is_same_v<std::remove_cv_t<T>, Atomic128<Pattern>>);
  Pattern observed;
  for (int i = 0; i < NumberOfIterations * 2; ++i) {
    observed = atomic.load(order);
    if (not observed.is_valid()) {
      error = true;
      return;
    }
  }
}

template <int NumberOfIterations, class T>
void exchange_thread(T& atomic, std::atomic<bool>& error, const Pattern& pat,
                     const std::memory_order order) {
  static_assert(std::is_same_v<std::remove_cv_t<T>, Atomic128<Pattern>>);
  Pattern observed;
  for (int i = 0; i < NumberOfIterations * 2; ++i) {
    observed = atomic.exchange(pat, order);
    if (not observed.is_valid()) {
      error = true;
      return;
    }
  }
}

/*
 * CAS thread template (for both weak and strong).
 * Repeatedly attempts a CAS from "from" pattern to "to" pattern.
 * Template parameter CasFunc: pointer to member function (weak or strong).
 * Args:
 *   atomic: reference to the Atomic128 instance
 *   from: expected value for CAS
 *   to: desired value for CAS
 *   cas_func: pointer to member function for CAS
 *   success: success memory order
 *   failure: failure memory order
 */
template <bool Strong, int NumberOfIterations, class T>
void cas_thread(T& atomic, const Pattern& from, const Pattern& to,
                const std::memory_order success,
                const std::memory_order failure) {
  static_assert(std::is_same_v<std::remove_cv_t<T>, Atomic128<Pattern>>);
  for (int i = 0; i < NumberOfIterations; ++i) {
    Pattern expected = from;
    // Loop until CAS succeeds
    if constexpr (Strong) {
      while (not atomic.compare_exchange_strong(expected, to, success, failure))
        ;
    } else {
      while (not atomic.compare_exchange_weak(expected, to, success, failure))
        ;
    }
  }
}
template <bool Strong, int NumberOfIterations, class T>
void cas_thread(T& atomic, const Pattern& from, const Pattern& to,
                const std::memory_order order) {
  static_assert(std::is_same_v<std::remove_cv_t<T>, Atomic128<Pattern>>);
  for (int i = 0; i < NumberOfIterations; ++i) {
    Pattern expected = from;
    // Loop until CAS succeeds
    if constexpr (Strong) {
      while (not atomic.compare_exchange_strong(expected, to, order))
        ;
    } else {
      while (not atomic.compare_exchange_weak(expected, to, order))
        ;
    }
  }
}

/*
 * Test atomicity of store/load.
 * Launches writer and reader threads with varied memory orders and checks for
 * torn reads.
 */
template <Check check, bool VolatileAtomic, int NumberOfIterations>
void test_load_store(const int number_of_loads, const int number_of_stores,
                     const bool cas_single_arg = false) {
  CAPTURE(VolatileAtomic);
  CAPTURE(number_of_loads);
  CAPTURE(number_of_stores);
  CAPTURE(cas_single_arg);

  struct TestConf {
    std::memory_order store_order, load_order;
  };
  // Valid combinations for store/load orders
  const std::vector<TestConf> tests =
      check == Check::LoadStore
          ? std::vector<TestConf>{{std::memory_order_relaxed,
                                   std::memory_order_relaxed},
                                  {std::memory_order_relaxed,
                                   std::memory_order_acquire},
                                  {std::memory_order_release,
                                   std::memory_order_relaxed},
                                  {std::memory_order_release,
                                   std::memory_order_acquire},
                                  {std::memory_order_release,
                                   std::memory_order_seq_cst},
                                  {std::memory_order_seq_cst,
                                   std::memory_order_acquire},
                                  {std::memory_order_seq_cst,
                                   std::memory_order_seq_cst}}
          : (check == Check::Exchange
                 ? std::vector<TestConf>{{std::memory_order_relaxed,
                                          std::memory_order_relaxed},
                                         {std::memory_order_acquire,
                                          std::memory_order_relaxed},
                                         {std::memory_order_release,
                                          std::memory_order_relaxed},
                                         {std::memory_order_acq_rel,
                                          std::memory_order_relaxed},
                                         {std::memory_order_seq_cst,
                                          std::memory_order_relaxed}}

                 : std::vector<TestConf>{
                       {std::memory_order_relaxed, std::memory_order_relaxed},
                       {std::memory_order_seq_cst, std::memory_order_seq_cst},
                       {std::memory_order_release, std::memory_order_acquire},
                       {std::memory_order_acq_rel, std::memory_order_acquire}});

  for (const auto& conf : tests) {
    CAPTURE(conf.store_order);
    CAPTURE(conf.load_order);
    std::conditional_t<VolatileAtomic, volatile Atomic128<Pattern>,
                       Atomic128<Pattern>>
        atomic;
    std::atomic<bool> error{false};
    std::array<Pattern, Pattern::patterns.size()> patterns{};
    for (size_t i = 0; i < patterns.size(); ++i) {
      set_pattern(patterns[i], i);
    }
    atomic.store(patterns[0], std::memory_order_seq_cst);  // Initialize

    std::atomic<size_t> active_threads = {0};
    const auto sync = [&active_threads, number_of_loads, number_of_stores]() {
      (void)number_of_loads, (void)number_of_stores;
      active_threads.fetch_add(1, std::memory_order_relaxed);
      while (active_threads.load(std::memory_order_relaxed) !=
             static_cast<size_t>(number_of_loads + number_of_stores))
        ;
    };

    // Launch writer and reader threads
    std::vector<std::thread> threads;
    for (size_t i = 0; i < static_cast<size_t>(number_of_stores); ++i) {
      if constexpr (check == Check::LoadStore) {
        const size_t index = i % patterns.size();
        threads.emplace_back([&atomic, &patterns, &conf, &sync, index]() {
          sync();
          store_thread<NumberOfIterations>(atomic, patterns[index],
                                           conf.store_order);
        });
      } else if constexpr (check == Check::CasStrong or
                           check == Check::CasWeak) {
        const size_t index_to = i % patterns.size();
        const size_t index_from =
            (i == 0 ? static_cast<size_t>(number_of_stores) : (i - 1)) %
            patterns.size();
        threads.emplace_back([&atomic, &patterns, &conf, &sync, index_to,
                              index_from, cas_single_arg]() {
          sync();
          if (cas_single_arg) {
            cas_thread<check == Check::CasStrong, NumberOfIterations>(
                atomic, patterns[index_from], patterns[index_to],
                conf.store_order);
          } else {
            cas_thread<check == Check::CasStrong, NumberOfIterations>(
                atomic, patterns[index_from], patterns[index_to],
                conf.store_order, conf.load_order);
          }
        });
      } else if constexpr (check == Check::Exchange) {
        const size_t index = i % patterns.size();
        threads.emplace_back(
            [&atomic, &patterns, &conf, &sync, &error, index]() {
              sync();
              exchange_thread<NumberOfIterations>(
                  atomic, error, patterns[index], conf.store_order);
            });
      } else {
        REQUIRE(check != Check::Uninitialized);
      }
    }
    for (int i = 0; i < number_of_loads; ++i) {
      threads.emplace_back([&atomic, &error, &conf, &sync]() {
        sync();
        load_thread<NumberOfIterations>(atomic, error, conf.load_order);
      });
    }

    for (auto& t : threads) {
      t.join();
    }

    // If any reader saw a torn (invalid) value, error is set.
    CHECK_FALSE(error.load());
  }
}

int get_env_variable_with_default(const char* env_name, const int default_value) {
  const char* env_value = std::getenv(env_name);

  if (env_value == nullptr) {
    return default_value;
  }

  const std::string str_value(env_value);

  if (str_value.empty()) {
    return default_value;
  }

  try {
    size_t pos;
    const int value = std::stoi(str_value, &pos);

    if (pos != str_value.length()) {
      return default_value;
    }

    return value;
  } catch (...) {
    return default_value;
  }
}
}  // namespace

TEST_CASE("Atomic128") {
  const hardware_info::CpuInfo cpu_info = hardware_info::cpu_info();
  constexpr int number_of_iterations = 500'000;
  const int number_of_load_threads = get_env_variable_with_default(
      "FINDUS_ATOMIC128_LOAD_THREADS", cpu_info.number_of_processing_units / 2);
  const int number_of_store_threads =
      get_env_variable_with_default("FINDUS_ATOMIC128_STORE_THREADS",
                                    cpu_info.number_of_processing_units / 2);

  {
    using Ut = std::underlying_type_t<std::memory_order>;
    std::cout << std::boolalpha;
    std::cout << "Relaxed: " << static_cast<Ut>(std::memory_order_relaxed)
              << "\n";
    std::cout << "Acquire: " << static_cast<Ut>(std::memory_order_acquire)
              << "\n";
    std::cout << "Release: " << static_cast<Ut>(std::memory_order_release)
              << "\n";
    std::cout << "Sequentially consistent: "
              << static_cast<Ut>(std::memory_order_seq_cst) << "\n";
    std::cout << "\n" << std::flush;
  }

  std::cout << "Starting LoadStore\n" << std::flush;
  test_load_store<Check::LoadStore, false, number_of_iterations>(
      number_of_load_threads, number_of_store_threads);
  test_load_store<Check::LoadStore, true, number_of_iterations>(
      number_of_load_threads, number_of_store_threads);

  std::cout << "Starting Exchange\n" << std::flush;
  test_load_store<Check::Exchange, false, number_of_iterations>(
      number_of_load_threads, number_of_store_threads);
  test_load_store<Check::Exchange, true, number_of_iterations>(
      number_of_load_threads, number_of_store_threads);

  for (const bool cas_single_arg : {false, true}) {
    std::cout << "Starting CasStrong, single arg: " << cas_single_arg << "\n"
              << std::flush;
    test_load_store<Check::CasStrong, false, number_of_iterations>(
        number_of_load_threads, number_of_store_threads, cas_single_arg);
    test_load_store<Check::CasStrong, true, number_of_iterations>(
        number_of_load_threads, number_of_store_threads, cas_single_arg);

    std::cout << "Starting CasWeak, single arg: " << cas_single_arg << "\n"
              << std::flush;
    test_load_store<Check::CasWeak, false, number_of_iterations>(
        number_of_load_threads, number_of_store_threads, cas_single_arg);
    test_load_store<Check::CasWeak, true, number_of_iterations>(
        number_of_load_threads, number_of_store_threads, cas_single_arg);
  }
}
}  // namespace findus
#endif
