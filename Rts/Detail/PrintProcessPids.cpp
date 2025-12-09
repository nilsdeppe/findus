// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/Detail/PrintProcessPids.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>

namespace findus::detail {
void print_process_pids(const int process_id) {
  const char* env_enable_pid_print =
      // NOLINTNEXTLINE(concurrency-mt-unsafe)
      std::getenv("FINDUS_PRINT_PID");
  if (env_enable_pid_print == nullptr) {
    return;
  }
  const std::string output_info =
      std::string{"   pid:"} + std::to_string(getpid()) +
      " RTS pid:" + std::to_string(process_id) + "\n";
  std::cout << output_info << std::flush;
}
}  // namespace findus::detail
