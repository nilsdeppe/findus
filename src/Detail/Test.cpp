// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#if defined(FINDUS_ENABLE_TESTING)

#include <doctest/extensions/doctest_mpi.h>

namespace {
bool has_findus_mpi_test_flag(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--findus-mpi-test") {
      return true;
    }
  }
  return false;
}
}  // namespace

namespace findus::detail {
// Necessary to get doctest to show unit tests with shared libs.
void findus_force_link_for_tests();
}  // namespace findus::detail

int main(int argc, char** argv) {
  findus::detail::findus_force_link_for_tests();
  if (has_findus_mpi_test_flag(argc, argv)) {
    // Note: the definitions are all in the _DocTestImpl library that we link.
    doctest::mpi_init_thread(argc, argv, MPI_THREAD_MULTIPLE);

    doctest::Context ctx;
    ctx.setOption("reporters", "MpiConsoleReporter");
    ctx.setOption("reporters", "MpiFileReporter");
    ctx.applyCommandLine(argc, argv);

    int test_result = ctx.run();

    doctest::mpi_finalize();

    return test_result;
  } else {
    doctest::Context ctx;
    ctx.applyCommandLine(argc, argv);

    int test_result = ctx.run();

    return test_result;
  }
}

#else
static_assert(false,
              "Should never compile Test.cpp when testing isn't enabled.");
#endif
