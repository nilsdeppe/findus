// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/DistributedTaskDriver.hpp"

#include <memory>
#include <mpi.h>
#include <string>

#include "Rts/Exception.hpp"
#include "Rts/MessageTags.hpp"
#include "Rts/MpiException.hpp"

namespace rts {
DistributedTaskDriver::DistributedTaskDriver(int* argc, char** argv[],
                                             bool initialize_mpi)
    : initialize_mpi_(initialize_mpi) {
  if (initialize_mpi_) {
    int mpi_threading_support = MPI_SUCCESS;
    if (MPI_Init_thread(argc, argv, MPI_THREAD_MULTIPLE,
                        &mpi_threading_support) != MPI_SUCCESS) {
      throw MpiException("Failed to initialize MPI");
    }
    if (mpi_threading_support == MPI_THREAD_SINGLE) {
      throw MpiException("Cannot use MPI with only MPI_THREAD_SINGLE support.");
    } else if (mpi_threading_support == MPI_THREAD_MULTIPLE) {
      mpi_supports_multithreading_ = true;
    }

  } else {
    int mpi_is_initialized = false;
    if (MPI_Initialized(&mpi_is_initialized) != MPI_SUCCESS) {
      throw MpiException("Failed to check if MPI is initialized.");
    }
    if (mpi_is_initialized == 0) {
      throw MpiException(
          "MPI is not initialized but DistributedTaskDriver was told not "
          "to.");
    }
    if (MPI_Comm_rank(rts_comm_, &my_node_id_) != MPI_SUCCESS) {
      throw MpiException(
          "Failed to get node rank in ToyRTS communicator. We don't yet have "
          "full support for not initializing MPI with ToyRTS.");
    }
  }

  if (MPI_Comm_dup(MPI_COMM_WORLD, &rts_comm_) != MPI_SUCCESS) {
    throw MpiException("Failed to set the rts communicator");
  }
  if (MPI_Comm_rank(rts_comm_, &my_node_id_) != MPI_SUCCESS) {
    throw MpiException("Failed to get my node ID in the ToyRTS communicator.");
  }

  if (MPI_Get_version(&mpi_version_, &mpi_subversion_) != MPI_SUCCESS) {
    throw MpiException("Failed to get the MPI version and subversion.");
  } else if (my_node_id_ == 0) {
    std::cout << "rts: Using MPI version " << mpi_version_ << '.'
              << mpi_subversion_ << ".\n";
  }

  if (my_node_id_ == 0) {
    if (mpi_supports_multithreading_) {
      std::cout << "rts: No communication thread required. All threads can "
                   "manage MPI.\n";
    } else {
      std::cout << "rts: Communication thread required. Main thread will "
                   "manage MPI.\n";
    }
  }

  if (MPI_Comm_size(rts_comm_, &number_of_nodes_) != MPI_SUCCESS) {
    throw MpiException(
        "Failed to get the number of nodes in the ToyRTS communicator.");
  }

  number_of_threads_ = 4;

  // Broadcast number of threads requested.
  if (MPI_Bcast(&number_of_threads_, 1, MPI_INT, 0, rts_comm_) != MPI_SUCCESS) {
    throw MpiException("Failed to broadcast number of threads.");
  }

  thread_pool_ = std::make_unique<ThreadPool_t>(
      static_cast<uint32_t>(number_of_threads_), 0, 4);
}

std::string DistributedTaskDriver::mpi_threading_to_string(
    const int mpi_threading) {
  switch (mpi_threading) {
    case MPI_THREAD_SINGLE:
      return "MPI_THREAD_SINGLE";
    case MPI_THREAD_FUNNELED:
      return "MPI_THREAD_FUNNELED";
    case MPI_THREAD_SERIALIZED:
      return "MPI_THREAD_SERIALIZED";
    case MPI_THREAD_MULTIPLE:
      return "MPI_THREAD_MULTIPLE";
    default:
      throw std::runtime_error("Unknown MPI threading support");
  };
}

void DistributedTaskDriver::anchor() {}

void DistributedTaskDriver::attach_debugger() {
  const char* env_enable_parallel_debug =
      // NOLINTNEXTLINE(concurrency-mt-unsafe)
      std::getenv("RTS_ATTACH_DEBUGGER");
  if (env_enable_parallel_debug != nullptr) {
    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    char hostname[2048];
    gethostname(static_cast<char*>(hostname), sizeof(hostname));

    const std::string output_info =
        std::string{"   pid:"} + std::to_string(getpid()) +
        std::string{":host:"} + std::string{static_cast<char*>(hostname)} +
        ":rank:" + std::to_string(current_node_id()) + "\n";
    // We send the output to rank 0 to print so that all the prints are done
    // in order without garbling output. Additionally, we serialize in rank
    // order.
    if (current_node_id() == 0) {
      std::cout
          << "Enabling attaching to a debugger. Below are the PIDs and\n"
             "host names for the different MPI ranks. On each host, you\n"
             "must attach GDB to the PID using 'gdb --pid=PID'. You must\n"
             "then interrupt and navigate up to this stack, at which\n"
             "point you can run 'set var i = 1' in GDB followed by\n"
             "'continue' continue the processes.\n";
      std::cout << output_info << std::flush;
      for (int node_id = 1; node_id < number_of_nodes(); ++node_id) {
        MPI_Status status{};
        if (const auto mpi_result = MPI_Probe(
                node_id, message_tags::debugger_attach, rts_comm_, &status);
            mpi_result != MPI_SUCCESS) {
          throw MpiException{
              "Could not call MPI_Probe in attach_debugger() for rank " +
              std::to_string(node_id)};
        }
        int count = 0;
        if (const auto mpi_result = MPI_Get_count(&status, MPI_CHAR, &count);
            mpi_result != MPI_SUCCESS) {
          throw MpiException{
              "Could not get count in attach_debugger() for rank " +
              std::to_string(node_id)};
        }
        if (count < 0) {
          throw Exception{
              "Received a negative size for the number of characters in the "
              "debugger attachment string: " +
              std::to_string(count)};
        }
        // Create string with an extra space of 4 for any termination
        // characters and overrun.
        std::string output(static_cast<size_t>(count + 4), '\0');
        if (const auto mpi_result = MPI_Recv(
                output.data(), count, MPI_CHAR, node_id,
                message_tags::debugger_attach, rts_comm_, MPI_STATUS_IGNORE);
            mpi_result != MPI_SUCCESS) {
          throw MpiException{
              "Could not receive data in attach_debugger() for rank " +
              std::to_string(node_id)};
        }
        std::cout << output << std::flush;
      }
    } else {
      if (const auto mpi_result =
              MPI_Send(output_info.data(), output_info.length(), MPI_CHAR, 0,
                       message_tags::debugger_attach, rts_comm_);
          mpi_result != MPI_SUCCESS) {
        throw MpiException(
            "Could not send message for attaching to debugger from rank " +
            std::to_string(current_node_id()));
      }
    }
    // NOLINTNEXTLINE(misc-const-correctness)
    volatile int i = 10;
    while (i == 10) {
      using namespace std::chrono_literals;
      std::this_thread::sleep_for(std::chrono::seconds{i});
    }
  }
}

DistributedTaskDriver& create_distributed_task_driver(int* argc,
                                                      char** argv[]) {
  if (task_driver != nullptr) {
    throw Exception(
        "Already initialized the task driver. You should only initialize the "
        "driver once.");
  }
  const_cast<std::unique_ptr<DistributedTaskDriver>&>(task_driver) =
      std::unique_ptr<DistributedTaskDriver>(
          new DistributedTaskDriver(argc, argv, true));
  return *task_driver.get();
}

namespace detail {
uint32_t distributed_object_index_counter = 0;
}  // namespace detail
}  // namespace rts
