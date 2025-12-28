// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <iosfwd>

#include "findus/BindTo.hpp"

namespace findus {
/*!
 * \brief Options for constructing the DistributedTaskDriver.
 */
struct Options {
  /*!
   * \brief Binding policy applied to each thread.
   */
  BindTo bind_to;
  /*!
   * \brief Number of task threads created per process.
   */
  int task_threads_per_process;
  /*!
   * \brief If `true` then the communication thread works on tasks when it is
   * not busy with communication.
   */
  bool comm_thread_do_tasks;
};

/// \brief Stream operator for Options.
std::ostream& operator<<(std::ostream& os, const Options& options);

/*!
 * \brief Parse process options from command-line flags.
 *
 * The parser expects every listed flag to appear exactly once. It throws
 * findus::Exception if a pointer is null, a flag is missing, or a value
 * cannot be parsed.
 *
 * | Flag                             | Description                    |
 * |----------------------------------|--------------------------------|
 * | --findus-task-threads-per-process| Integer thread count.          |
 * |                                  | Allowed values: any valid      |
 * |                                  | signed int.                    |
 * | --findus-bind-to                 | Binding policy.                |
 * |                                  | Allowed values: None, Core,    |
 * |                                  | HardwareThread.                |
 * | --findus-comm-thread-do-tasks    | If comm thread works on tasks  |
 * |                                  | when not otherwise.            |
 * |                                  | No value is passed.            |
 *
 */
Options from_command_line(const int* argc, char** argv[]);
}  // namespace findus
