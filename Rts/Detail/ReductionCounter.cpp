// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/Detail/ReductionCounter.hpp"

#include <string>

#include "Rts/Exceptions/Exception.hpp"

namespace rts::reduction::detail {
Counter::Counter(const size_t max_entries) : entries_(max_entries) {
  if (max_entries == 0) {
    throw Exception{
        "The maximum number of entries must be a power of two larger than 0. "
        "Got " +
        std::to_string(max_entries)};
  }
  if ((max_entries bitand (max_entries - 1)) != 0) {
    throw Exception{
        "The maximum number of entries must be a power of two larger than 0. "
        "Got " +
        std::to_string(max_entries)};
  }
  // Since atomics may not always be lock free depending on alignment, we want
  // to catch issues where the hardware cannot guarantee that the atomic is
  // lock free. It is not inherently bad that we cannot guarantee that the
  // atomics are lock free at compile time, we could check at runtime, but
  // it's nice to have the guarantee when possible. If for some reason the
  // guarantee isn't given, then likely forcing alignment of the individual
  // atomic variables would restore it. For example, some system may only be
  // able to handle atomics on 32-byte word boundaries.
  static_assert(decltype(KeyCount{}.key)::is_always_lock_free);
  static_assert(decltype(KeyCount{}.counter)::is_always_lock_free);
  // Explicitly zero out the counters.
  for (auto& t : entries_) {
    t.key.store(0, std::memory_order_relaxed);
    t.counter.store(0, std::memory_order_relaxed);
  }
}

Counter::~Counter() = default;
}  // namespace rts::reduction::detail

#if defined(RTS_ENABLE_TESTING)

#include <algorithm>
#include <atomic>
#include <doctest/doctest.h>
#include <random>
#include <thread>
#include <vector>

#include "Rts/Detail/ReductionCounter.hpp"

namespace rts::reduction::detail {
template <size_t NumThreads>
void test_counter_parallel(const size_t num_reductions,
                           const size_t max_entries = 0) {
  INFO("Test Counter with " << NumThreads << " threads and " << num_reductions
                            << " reductions");

  // Use a consistent seed for reproducibility.
  const size_t rng_seed = 4444;
  const std::uint32_t max_per_thread_contributions = 3;
  if (max_entries == 0) {
    const_cast<size_t&>(max_entries) = std::max(num_reductions * 2ul, 2ul);
  }
  Counter counter(max_entries);

  // Each thread will contribute to all reductions, with varying contributions
  std::vector<std::atomic<int>> completions(num_reductions);
  for (auto& c : completions) {
    c.store(0);
  }

  // Randomize contributions per thread for each reduction
  std::vector<std::vector<int>> contributions(
      NumThreads, std::vector<int>(num_reductions, 1));
  {
    std::mt19937 rng{rng_seed};
    for (size_t t = 0; t < NumThreads; ++t) {
      for (size_t r = 0; r < num_reductions; ++r) {
        contributions[t][r] = 1 + (rng() % max_per_thread_contributions);
      }
    }
  }
  // This is the total expected contributions for each reduction.
  std::vector<int> total_contributions(num_reductions, 0);
  for (size_t r = 0; r < num_reductions; ++r) {
    for (size_t t = 0; t < NumThreads; ++t) {
      total_contributions[r] += contributions[t][r];
    }
  }

  std::vector<std::thread> threads;
  threads.reserve(NumThreads);
  for (size_t t = 0; t < NumThreads; ++t) {
    threads.emplace_back([&, t] {
      for (size_t r = 0; r < num_reductions; ++r) {
        const int contrib = contributions[t][r];
        for (int i = 0; i < contrib; ++i) {
          if (counter.increment(static_cast<uint64_t>(r + 1),
                                [&total_contributions, r] {
                                  return total_contributions[r];
                                })) {
            completions[r].fetch_add(1, std::memory_order_relaxed);
          }
        }
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }

  // Check that each reduction completed exactly once
  for (size_t r = 0; r < num_reductions; ++r) {
    CHECK(completions[r].load() == 1);
  }
}

void test_counter_constructor_exceptions() {
  INFO("Test Counter constructor throws for invalid max_entries");

  // Test zero max_entries
  CHECK_THROWS_WITH_AS(Counter(0),
                       "The maximum number of entries must be a power of two "
                       "larger than 0. Got 0",
                       rts::Exception);

  // Test non-power-of-two max_entries (e.g., 3)
  CHECK_THROWS_WITH_AS(Counter(3),
                       "The maximum number of entries must be a power of two "
                       "larger than 0. Got 3",
                       rts::Exception);

  // Test another non-power-of-two (e.g., 5)
  CHECK_THROWS_WITH_AS(Counter(5),
                       "The maximum number of entries must be a power of two "
                       "larger than 0. Got 5",
                       rts::Exception);
}

void test_counter_increment_zero_key_exception() {
  INFO("Test Counter::increment throws for key value 0");

  Counter counter(2);

  // Attempt to increment with key 0, should throw
  CHECK_THROWS_WITH_AS(counter.increment(0, [] { return 10; }),
                       "The key value of 0 is not supported in reductions "
                       "because it is used as a sentinel.",
                       rts::Exception);
}

void test_counter_full_exception() {
  INFO("Test Counter throws when container is full");
  constexpr size_t max_entries = 4;
  Counter counter(max_entries);

  // Fill all slots with unique keys
  for (size_t i = 1; i <= max_entries; ++i) {
    const bool complete = counter.increment(i, [] { return 2; });
    CHECK_FALSE(complete);
  }

  // Now try to insert another key, should throw with a specific message
  CHECK_THROWS_WITH_AS(counter.increment(max_entries + 1, [] { return 1; }),
                       "Failed to insert hashed key 5 because the container is "
                       "full. Max entries is 4",
                       rts::Exception);
}
}  // namespace rts::reduction::detail

TEST_CASE("ReductionCounter") {
  for (const auto number_of_reductions :
       {0ul, 1ul, 2ul, 8ul, 16ul, 128ul, 512ul}) {
    rts::reduction::detail::test_counter_parallel<1>(number_of_reductions);
    rts::reduction::detail::test_counter_parallel<2>(number_of_reductions);
    rts::reduction::detail::test_counter_parallel<4>(number_of_reductions);
    rts::reduction::detail::test_counter_parallel<32>(number_of_reductions);
    rts::reduction::detail::test_counter_parallel<128>(number_of_reductions);

    // Test case where vector can get full to cause high pressure on hash.
    rts::reduction::detail::test_counter_parallel<4>(
        number_of_reductions, std::max(number_of_reductions, 2ul));
  }
  rts::reduction::detail::test_counter_constructor_exceptions();
  rts::reduction::detail::test_counter_increment_zero_key_exception();
  rts::reduction::detail::test_counter_full_exception();
}

#endif
