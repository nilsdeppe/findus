// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/DistributedTaskDriver.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <mpi.h>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>

#include "Rts/Callback.hpp"
#include "Rts/Detail/ActiveObject.hpp"
#include "Rts/Detail/DistributedObjectIndex.hpp"
#include "Rts/Detail/GetOutput.hpp"
#include "Rts/Detail/MpiErrorMessage.hpp"
#include "Rts/Detail/PrintProcessPids.hpp"
#include "Rts/Exceptions/Exception.hpp"
#include "Rts/Exceptions/Mpi.hpp"
#include "Rts/HardwareInfo.hpp"
#include "Rts/Message.hpp"
#include "Rts/MessageHeader.hpp"
#include "Rts/MessageTags.hpp"
#include "Rts/ParentAndChildren.hpp"
#include "Rts/Reduction.hpp"

namespace rts {
DistributedTaskDriver::DistributedTaskDriver(bool finalize_mpi,
                                             bool mpi_supports_multithreading)
    : finalize_mpi_(finalize_mpi),
      mpi_supports_multithreading_(mpi_supports_multithreading) {
  int mpi_is_initialized = false;
  if (MPI_Initialized(&mpi_is_initialized) != MPI_SUCCESS) {
    throw MpiException("Failed to check if MPI is initialized.");
  }
  if (mpi_is_initialized == 0) {
    throw MpiException(
        "MPI is not initialized but DistributedTaskDriver was told not "
        "to.");
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
      static_cast<uint32_t>(number_of_threads_), 1, this);
  hardware_info::print_hardware_info(rts_comm_);

  active_object_.resize(static_cast<size_t>(number_of_threads_ + 1),
                        detail::ActiveObject{});

  parent_and_children_ =
      detail::parent_and_children(current_node_id(), number_of_nodes());
  if (parent_and_children_.parent_process_id != -1) {
    const auto p_and_c_of_parent = detail::parent_and_children(
        parent_and_children_.parent_process_id, number_of_nodes());
    parent_other_child_process_id_ =
        p_and_c_of_parent.left_process_id == current_node_id()
            ? p_and_c_of_parent.right_process_id
            : p_and_c_of_parent.left_process_id;
  }
  if (parent_and_children_.left_process_id != -1) {
    all_left_children_ = detail::children_in_subtree(
        parent_and_children_.left_process_id, number_of_nodes());
    all_left_children_.push_back(parent_and_children_.left_process_id);
    std::sort(all_left_children_.begin(), all_left_children_.end());
  }
  if (parent_and_children_.right_process_id != -1) {
    all_right_children_ = detail::children_in_subtree(
        parent_and_children_.right_process_id, number_of_nodes());
    all_right_children_.push_back(parent_and_children_.right_process_id);
    std::sort(all_right_children_.begin(), all_right_children_.end());
  }
  if (const auto parent_pid = parent_and_children_.parent_process_id;
      parent_pid != -1) {
    const auto parents_info =
        detail::parent_and_children(parent_pid, number_of_nodes());
    if (parents_info.left_process_id != parent_and_children_.self_process_id and
        parents_info.right_process_id != parent_and_children_.self_process_id) {
      throw Exception{
          "Either the left (" + std::to_string(parents_info.left_process_id) +
          ") or right (" + std::to_string(parents_info.right_process_id) +
          ") child of parent process (" + std::to_string(parent_pid) +
          ") should be the self (current child) process " +
          std::to_string(parent_and_children_.self_process_id)};
    }
    if (parents_info.left_process_id != parent_and_children_.self_process_id and
        parents_info.left_process_id != -1) {
      parent_other_subtree_children_ = detail::children_in_subtree(
          parents_info.left_process_id, number_of_nodes());
      parent_other_subtree_children_.push_back(parents_info.left_process_id);
    } else if (parents_info.right_process_id !=
                   parent_and_children_.self_process_id and
               parents_info.right_process_id != -1) {
      parent_other_subtree_children_ = detail::children_in_subtree(
          parents_info.right_process_id, number_of_nodes());
      parent_other_subtree_children_.push_back(parents_info.right_process_id);
    }
    std::sort(parent_other_subtree_children_.begin(),
              parent_other_subtree_children_.end());
  }

  // Set global quiescence detection bookkeeping.
  global_qd_ = qd::Global{current_node_id(), number_of_nodes(), 10};

  per_process_broadcast_to_number_of_elements_.assign(
      static_cast<size_t>(number_of_threads_) + 1,
      std::vector<int>(static_cast<size_t>(number_of_nodes())));
  // Set the thread_id_ of the communication thread to 0.
  thread_id_ = 0;
}

DistributedTaskDriver::~DistributedTaskDriver() noexcept {
  int mpi_is_finalized = 1;
  if (const auto mpi_result = MPI_Finalized(&mpi_is_finalized);
      mpi_result != MPI_SUCCESS) {
    std::cout << "Failed to check if MPI is finalized.\n" << std::flush;
    mpi_is_finalized = 1;
  }
  if (not mpi_is_finalized) {
    if (const auto mpi_result = MPI_Comm_free(&rts_comm_);
        mpi_result != MPI_SUCCESS) {
      std::cout << "Failed to free RTS communicator.\n" << std::flush;
    }
  }
  if (finalize_mpi_) {
    MPI_Finalize();
  }
}

void DistributedTaskDriver::launch_threads(
    const std::optional<uint32_t> thread_for_logging) {
  if (in_insert_mode_) {
    throw Exception{
        "Cannot call driver.launch_threads() while still in Insert mode. You "
        "must first call driver.insert_barrier() on all processes."};
  }
  if (thread_pool_->threads_are_active()) {
    throw Exception{
        "Threads are already active. You cannot launch threads when they are "
        "already running. Process ID: " +
        std::to_string(current_node_id())};
  }
  thread_pool_->launch_threads(thread_for_logging);
}

void DistributedTaskDriver::force_threads_to_stop() { thread_pool_->stop(); }

bool DistributedTaskDriver::is_locally_quiescent() {
  return thread_pool_->is_quiescent();
}

void DistributedTaskDriver::insert_barrier(
    const bool check_consistency_across_processes) const {
  if (check_consistency_across_processes) {
    barrier();
    check_component_accounting_consistency();
  }
  barrier();
  const_cast<bool&>(in_insert_mode_) = false;
}

void DistributedTaskDriver::barrier() const {
  if (const auto mpi_result = MPI_Barrier(rts_comm_);
      mpi_result != MPI_SUCCESS) {
    throw MpiException{"Failed to call barrier on process " +
                       std::to_string(current_node_id())};
  }
}

void DistributedTaskDriver::run_to_quiescence(const int max_to_receive,
                                              const int max_to_send) {
  if (in_insert_mode_) {
    throw Exception{
      "Cannot call driver.run_to_quiescence() while still in Insert mode. "
        "You must first call driver.insert_barrier() on all processes.  "
        "Process ID: " +
        std::to_string(current_node_id())};
  }
  if (not thread_pool_->threads_are_active()) {
    throw Exception{
      "Cannot call driver.run_to_quiescence() before "
        "driver.launch_threads(). Process ID: " +
        std::to_string(current_node_id())};
  }
  int local_qd_counter = 0;
  while (true) {
    initiate_receives(max_to_receive);
    initiate_sends(max_to_send);
    clean_incoming_mpi_messages();
    clean_outgoing_mpi_messages();
    // We check local QD first. If we have local QD, then we increment the
    // local QD counter. If the local QD counter reaches
    // local_qd_counts_for_global_qd, then we do a global QD check. This is so
    // that we don't check global QD too frequently and are "very sure" we
    // have local QD.
    if (is_locally_quiescent()) {
      ++local_qd_counter;
      if (local_qd_counter >= local_qd_counts_for_global_qd_) {
        local_qd_counter = 0;
        if (global_qd_.check(rts_comm_)) {
          return;
        }
      }
    }
  }
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
      throw MpiException("Unknown MPI threading support");
  };
}

namespace {
std::vector<int> split_string_as_ints(const std::string& str,
                                      const char delimiter) {
  std::vector<int> result;
  std::stringstream stream(str);
  std::string item;

  while (std::getline(stream, item, delimiter)) {
    result.push_back(std::stoi(item));
  }

  return result;
}
}  // namespace

void DistributedTaskDriver::attach_debugger() {
  const char* env_enable_parallel_debug =
      // NOLINTNEXTLINE(concurrency-mt-unsafe)
      std::getenv("RTS_ATTACH_DEBUGGER");
  if (env_enable_parallel_debug == nullptr) {
    return;
  }
  // NOLINTNEXTLINE(modernize-avoid-c-arrays)
  char hostname[2048];
  gethostname(static_cast<char*>(hostname), sizeof(hostname));

  const std::string debugger_request{env_enable_parallel_debug};
  for (const char ch : debugger_request) {
    if (not std::isdigit(ch) and ch != ',' and ch != '-') {
      throw Exception{
          "The environment variable RTS_ATTACH_DEBUGGER must contain only "
          "numbers or ',' but is set to: " +
          debugger_request};
    }
  }

  const std::vector<int> nodes_to_attach_on =
      split_string_as_ints(debugger_request, ',');

  if (nodes_to_attach_on.empty()) {
    throw Exception{
        "Received an empty list of nodes to attach a debugger to. You must "
        "specify a comma separated list of node IDs to attach on. You can "
        "specify '-1' to attach on all nodes. RTS_ATTACH_DEBUGGER is " +
        debugger_request};
  }

  if (nodes_to_attach_on.size() > 1) {
    for (const int node_id : nodes_to_attach_on) {
      if (node_id == -1) {
        throw Exception{
            "Cannot request all nodes for debugging (-1) and also specify "
            "specific nodes. RTS_ATTACH_DEBUGGER is " +
            debugger_request};
      } else if (node_id >= number_of_nodes()) {
        throw Exception{"Cannot request to debug on a node ID (" +
                        std::to_string(node_id) +
                        ") greater than the number of "
                        "nodes (" +
                        std::to_string(number_of_nodes()) +
                        ") . RTS_ATTACH_DEBUGGER is " + debugger_request};
      } else if (node_id < -1) {
        throw Exception{
            "Cannot request to debug on a node ID (" + std::to_string(node_id) +
            ") less than -1. RTS_ATTACH_DEBUGGER is " + debugger_request};
      }
    }
  } else {
    const int node_id = nodes_to_attach_on[0];
    if (node_id >= number_of_nodes()) {
      throw Exception{"Cannot request to debug on a node ID (" +
                      std::to_string(node_id) +
                      ") greater than the number of "
                      "nodes (" +
                      std::to_string(number_of_nodes()) +
                      ") . RTS_ATTACH_DEBUGGER is " + debugger_request};
    } else if (node_id < -1) {
      throw Exception{
          "Cannot request to debug on a node ID (" + std::to_string(node_id) +
          ") less than -1. RTS_ATTACH_DEBUGGER is " + debugger_request};
    }
  }

  const std::string output_info =
      std::string{"   pid:"} + std::to_string(getpid()) +
      std::string{" host:"} + std::string{static_cast<char*>(hostname)} +
      " rank:" + std::to_string(current_node_id()) +
      " gdb --pid=" + std::to_string(getpid()) + "\n";
  // We send the output to rank 0 to print so that all the prints are done
  // in order without garbling output. Additionally, we serialize in rank
  // order.
  if (current_node_id() == 0) {
    std::cout << "Enabling attaching to a debugger. Below are the PIDs and\n"
                 "host names for the different MPI ranks. On each host, you\n"
                 "must attach GDB to the PID using 'gdb --pid=PID'. You must\n"
                 "then interrupt and navigate up to this stack, at which\n"
                 "point you can run 'set var i = 1' in GDB followed by\n"
                 "'continue' continue the processes.\n";
    if (nodes_to_attach_on[0] == -1 or
        std::find(nodes_to_attach_on.begin(), nodes_to_attach_on.end(),
                  current_node_id()) != nodes_to_attach_on.end()) {
      std::cout << output_info << std::flush;
    }
    for (int node_id = 1; node_id < number_of_nodes(); ++node_id) {
      if (nodes_to_attach_on[0] != -1 and
          std::find(nodes_to_attach_on.begin(), nodes_to_attach_on.end(),
                    node_id) == nodes_to_attach_on.end()) {
        continue;
      }

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
  } else if (nodes_to_attach_on[0] == -1 or
             std::find(nodes_to_attach_on.begin(), nodes_to_attach_on.end(),
                       current_node_id()) != nodes_to_attach_on.end()) {
    if (const auto mpi_result =
            MPI_Send(output_info.data(), output_info.length(), MPI_CHAR, 0,
                     message_tags::debugger_attach, rts_comm_);
        mpi_result != MPI_SUCCESS) {
      throw MpiException(
          "Could not send message for attaching to debugger from rank " +
          std::to_string(current_node_id()));
    }
  }
  if (nodes_to_attach_on[0] == -1 or
      std::find(nodes_to_attach_on.begin(), nodes_to_attach_on.end(),
                current_node_id()) != nodes_to_attach_on.end()) {
    // NOLINTNEXTLINE(misc-const-correctness)
    volatile int i = 10;
    while (i == 10) {
      using namespace std::chrono_literals;
      std::this_thread::sleep_for(std::chrono::seconds{i});
    }
  }
}

namespace {
/*!
 * \brief Reads a value of type T from the buffer and advances the pointer.
 *
 * Copies sizeof(T) bytes from the memory at ptr into a T, then advances
 * ptr by sizeof(T).
 *
 * \tparam T The type to read from the buffer.
 * \param[in,out] ptr Reference to a pointer to the buffer position.
 *                   Advanced by sizeof(T) after reading.
 * \return The value of type T read from the buffer.
 */
template <typename T>
T read_and_advance(const char*& ptr) {
  T value;
  std::memcpy(&value, ptr, sizeof(T));
  ptr += sizeof(T);
  return value;
}

/*!
 * \brief Reads a string of given length from the buffer and advances ptr.
 *
 * Constructs a std::string from the next \a length bytes at ptr, then
 * advances ptr by \a length.
 *
 * \param[in,out] ptr Reference to a pointer to the buffer position.
 *                   Advanced by \a length after reading.
 * \param length The number of bytes to read as the string.
 * \return The string read from the buffer.
 */
std::string read_string_and_advance(const char*& ptr,
                                    const std::uint32_t length) {
  std::string str(ptr, ptr + length);
  ptr += length;
  return str;
}

/*!
 * \brief Compares two component accounting buffers for consistency.
 *
 * Compares two serialized component accounting buffers, usually from
 * serialize_component_accounting(), to ensure distributed object
 * registration is consistent between two processes. Checks the number
 * and names of regular and collection components, as well as collection
 * elements and their process IDs. Throws if any inconsistency is found.
 *
 * \param buffer_a The first buffer (from process_a).
 * \param process_a The process ID for buffer_a.
 * \param buffer_b The second buffer (from process_b).
 * \param process_b The process ID for buffer_b.
 *
 * \throws rts::Exception if any inconsistency is detected.
 */
void compare_component_accounting_buffers(const std::vector<char>& buffer_a,
                                          const int process_a,
                                          const std::vector<char>& buffer_b,
                                          const int process_b) {
  if (buffer_a.empty() and not buffer_b.empty()) {
    throw Exception{"Insertion error: Process " + std::to_string(process_a) +
                    " has none while process " + std::to_string(process_b) +
                    " has non-zero."};
  }
  if (buffer_b.empty() and not buffer_a.empty()) {
    throw Exception{"Insertion error: Process " + std::to_string(process_b) +
                    " has none while process " + std::to_string(process_a) +
                    " has non-zero."};
  }

  const char* ptr_a = buffer_a.data();
  const char* ptr_b = buffer_b.data();

  // Compare number of regular and collection components
  const std::uint32_t number_of_regular_components_a =
      read_and_advance<std::uint32_t>(ptr_a);
  const std::uint32_t number_of_regular_components_b =
      read_and_advance<std::uint32_t>(ptr_b);
  if (number_of_regular_components_a != number_of_regular_components_b) {
    throw Exception(
        "Insertion error: The number of regular components is different on "
        "different processes. This means you have different "
        "rts::insert_parallel_component calls on different processes. "
        "Process " +
        std::to_string(process_a) + " has " +
        std::to_string(number_of_regular_components_a) + ", process " +
        std::to_string(process_b) + " has " +
        std::to_string(number_of_regular_components_b));
  }

  const std::uint32_t number_of_collection_components_a =
      read_and_advance<std::uint32_t>(ptr_a);
  const std::uint32_t number_of_collection_components_b =
      read_and_advance<std::uint32_t>(ptr_b);
  if (number_of_collection_components_a != number_of_collection_components_b) {
    throw Exception(
        "Insertion error: The number of collection components is different on "
        "different processes. This means you have different "
        "rts::insert_parallel_component_collection calls on different "
        "processes. Process " +
        std::to_string(process_a) + " has " +
        std::to_string(number_of_collection_components_a) + ", process " +
        std::to_string(process_b) + " has " +
        std::to_string(number_of_collection_components_b));
  }

  // Compare regular component names
  for (std::uint32_t i = 0; i < number_of_regular_components_a; ++i) {
    const std::uint32_t name_length_a = read_and_advance<std::uint32_t>(ptr_a);
    const std::uint32_t name_length_b = read_and_advance<std::uint32_t>(ptr_b);
    const std::string name_a = read_string_and_advance(ptr_a, name_length_a);
    const std::string name_b = read_string_and_advance(ptr_b, name_length_b);
    if (name_a != name_b) {
      throw Exception("Insertion error: Regular component " +
                      std::to_string(i) + " name differs: process " +
                      std::to_string(process_a) + " has '" + name_a +
                      "', process " + std::to_string(process_b) + " has '" +
                      name_b +
                      "'. You must have inserted the components in a different "
                      "order on different processes.");
    }
  }

  // Compare collection component names and their elements
  for (std::uint32_t i = 0; i < number_of_collection_components_a; ++i) {
    const std::uint32_t name_length_a = read_and_advance<std::uint32_t>(ptr_a);
    const std::uint32_t name_length_b = read_and_advance<std::uint32_t>(ptr_b);
    const std::string name_a = read_string_and_advance(ptr_a, name_length_a);
    const std::string name_b = read_string_and_advance(ptr_b, name_length_b);
    if (name_a != name_b) {
      throw Exception("Insertion error: Collection component " +
                      std::to_string(i) + " name differs: process " +
                      std::to_string(process_a) + " has '" + name_a +
                      "', process " + std::to_string(process_b) + " has '" +
                      name_b + "'");
    }

    // Compare collection elements
    const std::uint64_t number_of_elements_a =
        read_and_advance<std::uint64_t>(ptr_a);
    const std::uint64_t number_of_elements_b =
        read_and_advance<std::uint64_t>(ptr_b);
    if (number_of_elements_a != number_of_elements_b) {
      throw Exception("Insertion error: Collection component '" + name_a +
                      "' number of elements differs: process " +
                      std::to_string(process_a) + " has " +
                      std::to_string(number_of_elements_a) + ", process " +
                      std::to_string(process_b) + " has " +
                      std::to_string(number_of_elements_b));
    }
    for (std::uint32_t j = 0; j < number_of_elements_a; ++j) {
      const std::uint64_t element_id_a = read_and_advance<std::uint64_t>(ptr_a);
      const std::uint64_t element_id_b = read_and_advance<std::uint64_t>(ptr_b);
      if (element_id_a != element_id_b) {
        throw Exception("Insertion error: Collection component '" + name_a +
                        "', element " + std::to_string(j) +
                        " ID differs: process " + std::to_string(process_a) +
                        " has " + std::to_string(element_id_a) + ", process " +
                        std::to_string(process_b) + " has " +
                        std::to_string(element_id_b));
      }
      const int process_id_a = read_and_advance<int>(ptr_a);
      const int process_id_b = read_and_advance<int>(ptr_b);
      if (process_id_a != process_id_b) {
        throw Exception("Insertion error: Collection component '" + name_a +
                        "', element " + std::to_string(j) + " (ID " +
                        std::to_string(element_id_a) + ") is on process " +
                        std::to_string(process_id_a) + " for process " +
                        std::to_string(process_a) + " but on process " +
                        std::to_string(process_id_b) + " for process " +
                        std::to_string(process_b));
      }
    }
  }
  // If we reach here, the buffers are identical.
}
}  // namespace

std::vector<char> DistributedTaskDriver::serialize_component_accounting()
    const {
  // Count regular and collection components
  size_t number_of_regular_components = 0;
  size_t number_of_collection_components = 0;
  size_t number_of_collection_elements = 0;
  for (const auto& object : distributed_objects_) {
    if (object.objects.index() == rts::detail::Regular) {
      ++number_of_regular_components;
    } else if (object.objects.index() == rts::detail::Collection) {
      ++number_of_collection_components;
      number_of_collection_elements +=
          std::get<rts::detail::Collection>(object.objects).size();
    }
  }

  // Estimate: 2 ints for counts, plus some space for names and elements.
  // - We estimate 100 character names at most.
  // - We add 8 bytes for the uint64_t for each collection element plus 4
  //   bytes for the process ID.
  const size_t estimated_size = 2 * sizeof(int) +
                                number_of_regular_components * 100 +
                                number_of_collection_components * 100 +
                                number_of_collection_elements * (8 + 4);
  std::vector<char> buffer;
  buffer.reserve(estimated_size);

  // Write counts
  buffer.insert(buffer.end(),
                reinterpret_cast<const char*>(&number_of_regular_components),
                reinterpret_cast<const char*>(&number_of_regular_components) +
                    sizeof(int));
  buffer.insert(
      buffer.end(),
      reinterpret_cast<const char*>(&number_of_collection_components),
      reinterpret_cast<const char*>(&number_of_collection_components) +
          sizeof(int));

  // Serialize regular component names
  for (const auto& object : distributed_objects_) {
    if (object.objects.index() == rts::detail::Regular) {
      const uint32_t name_length = static_cast<uint32_t>(object.name.size());
      buffer.insert(
          buffer.end(), reinterpret_cast<const char*>(&name_length),
          reinterpret_cast<const char*>(&name_length) + sizeof(uint32_t));
      buffer.insert(buffer.end(), object.name.begin(), object.name.end());
    }
  }

  // Serialize collection component names and their elements
  for (const auto& object : distributed_objects_) {
    if (object.objects.index() == rts::detail::Collection) {
      // First add name of collection
      const uint32_t name_length = static_cast<uint32_t>(object.name.size());
      buffer.insert(
          buffer.end(), reinterpret_cast<const char*>(&name_length),
          reinterpret_cast<const char*>(&name_length) + sizeof(uint32_t));
      buffer.insert(buffer.end(), object.name.begin(), object.name.end());

      // Get and sort collection elements. We only sort based on the
      // collection index since we should have the same number of those across
      // all process and the same ones. That makes it easier to check if the
      // PIDs are different.
      const auto& collection_map = std::get<1>(object.objects);
      std::vector<std::pair<uint64_t, int>> elements;
      elements.reserve(collection_map.size());
      for (const auto& [collection_index, holder] : collection_map) {
        elements.emplace_back(collection_index, holder.process_id);
      }
      std::sort(elements.begin(), elements.end(),
                [](const std::pair<uint64_t, int>& lhs,
                   const std::pair<uint64_t, int>& rhs) {
                  return lhs.first < rhs.first;
                });

      // Now add the element data to the buffer.
      const std::uint64_t number_of_elements = elements.size();
      buffer.insert(buffer.end(),
                    reinterpret_cast<const char*>(&number_of_elements),
                    reinterpret_cast<const char*>(&number_of_elements) +
                        sizeof(std::uint64_t));
      // Note: reserve() only increases if necessary and never shrinks.
      buffer.reserve(buffer.size() +
                     elements.size() * (sizeof(uint64_t) + sizeof(int)));
      for (const auto& [collection_index, process_id] : elements) {
        buffer.insert(buffer.end(),
                      reinterpret_cast<const char*>(&collection_index),
                      reinterpret_cast<const char*>(&collection_index) +
                          sizeof(uint64_t));
        buffer.insert(buffer.end(), reinterpret_cast<const char*>(&process_id),
                      reinterpret_cast<const char*>(&process_id) + sizeof(int));
      }
    }
  }
  return buffer;
}

void DistributedTaskDriver::check_component_accounting_consistency() const {
  // Serialize local component accounting.
  const std::vector<char> local_buffer = serialize_component_accounting();
  const int local_buffer_size = static_cast<int>(local_buffer.size());

  // Helper lambda to receive and compare data from a child using MPI_Probe.
  auto receive_and_compare = [&](const int child_id) {
    if (child_id == -1) {
      return;
    }
    // Probe for the incoming message to get its size.
    MPI_Status status;
    if (const auto mpi_result =
            MPI_Probe(child_id, message_tags::insert_consistency_check,
                      rts_comm_, &status);
        mpi_result != MPI_SUCCESS) {
      throw MpiException{
          "Failed to call MPI_Probe during insert_barrier()'s consistency "
          "check on process " +
          std::to_string(current_node_id())};
    }
    int child_buffer_size = 0;
    if (const auto mpi_result =
            MPI_Get_count(&status, MPI_CHAR, &child_buffer_size);
        mpi_result != MPI_SUCCESS) {
      throw MpiException{
          "Failed to call MPI_Get_count during insert_barrier()'s consistency "
          "check on process " +
          std::to_string(current_node_id())};
    }

    // Receive the buffer itself.
    std::vector<char> child_buffer(static_cast<size_t>(child_buffer_size));
    if (child_buffer_size > 0) {
      if (const auto mpi_result =
              MPI_Recv(child_buffer.data(), child_buffer_size, MPI_CHAR,
                       child_id, message_tags::insert_consistency_check,
                       rts_comm_, MPI_STATUS_IGNORE);
          mpi_result != MPI_SUCCESS) {
        throw MpiException{
            "Failed to call MPI_Recv during insert_barrier()'s consistency "
            "check on process " +
            std::to_string(current_node_id())};
      }
    }

    // Compare contents.
    compare_component_accounting_buffers(local_buffer, current_node_id(),
                                         child_buffer, child_id);
  };

  // If this is not the root, send our data to our parent as a single message.
  //
  // We send non-blocking but receive blocking. The code is a lot simpler this
  // way and this asymmetry is an optimal balance since we need to wait for
  // both receives to complete before we can continue anyway. The non-blocking
  // sends allow all processes to proceed at whatever rate they can.
  MPI_Request request;
  const int parent_id = parent_and_children_.parent_process_id;
  if (parent_id != -1) {
    if (local_buffer_size > 0) {
      if (const auto mpi_result = MPI_Isend(
              local_buffer.data(), local_buffer_size, MPI_CHAR, parent_id,
              message_tags::insert_consistency_check, rts_comm_, &request);
          mpi_result != MPI_SUCCESS) {
        throw MpiException{
            "Failed to call MPI_Send during insert_barrier()'s consistency "
            "check on process " +
            std::to_string(current_node_id())};
      }
    }
  }

  // Receive and compare from left and right children, if they exist.
  const int left_child_id = parent_and_children_.left_process_id;
  const int right_child_id = parent_and_children_.right_process_id;
  receive_and_compare(left_child_id);
  receive_and_compare(right_child_id);

  // now wait for our Isend to complete
  if (parent_id != -1 and local_buffer_size > 0) {
    MPI_Wait(&request, MPI_STATUS_IGNORE);
  }

  // If this is the root, and we reach here, all data matched.
}

void DistributedTaskDriver::invoke(Message_t& message,
                                   const uint32_t thread_id) {
  thread_id_ = thread_id_offset_ + thread_id;
  MessageHeader* message_header = message.get_header();
  if (thread_id_ >= active_object_.size()) {
    throw Exception{"Received thread ID " + std::to_string(thread_id) +
                    " but we initialized assuming at most " +
                    std::to_string(active_object_.size()) + " threads."};
  }
  active_object_[thread_id_] =
      detail::ActiveObject{message_header->distributed_object_index(),
                           message_header->target_collection_index()};
  (this->*threaded_action_absolute_ptr(message_header->member_function_ptr()))(
      message);
  active_object_[thread_id_] = detail::ActiveObject{};
}

void DistributedTaskDriver::send_data(const int target_node,
                                      Message_t message) {
  if (target_node == my_node_id_) {
    thread_pool_->add_task(std::move(message));
  } else {
    outgoing_messages_.enqueue(
        std::tuple<int, Message_t>{target_node, std::move(message)});
  }
}

void DistributedTaskDriver::send_message_impl(Message_t in_message) {
  if (in_message.message == nullptr) {
    throw Exception{
        "The message passed in is a nullptr. This is an internal error."};
  }
  const auto destination_process_id =
      in_message.get_header()->destination_process_id();
  if (destination_process_id < 0 or
      destination_process_id >= number_of_nodes()) {
    throw Exception{"The destination process ID (" +
                    std::to_string(destination_process_id) +
                    ") is outside of the range [0," +
                    std::to_string(number_of_nodes()) + ")."};
  }
  outgoing_mpi_messages_.push_back(
      std::tuple<std::optional<MPI_Request>, Message_t>{MPI_Request{},
                                                        std::move(in_message)});
  if (not std::get<0>(outgoing_mpi_messages_.back()).has_value()) {
    throw Exception{
        "The outgoing MPI message's MPI_Request is not set but it should "
        "be. This is an internal error."};
  }
  MPI_Request& request = std::get<0>(outgoing_mpi_messages_.back()).value();
  Message_t& message = std::get<1>(outgoing_mpi_messages_.back());
  MessageHeader& message_header = *message.get_header();
  const int num_bytes =
      static_cast<int>(message_header.number_of_bytes_in_message());
  if (num_bytes < 0) {
    throw Exception{
        "The size of the outgoing message is negative, which could be "
        "because the message is too large. Message size is " +
        std::to_string(message_header.number_of_bytes_in_message())};
  }
  if (const auto mpi_result = MPI_Isend(
          message.message.get(), num_bytes, MPI_BYTE, destination_process_id,
          message_tags::regular, rts_comm_, &request);
      mpi_result != MPI_SUCCESS) {
    throw MpiException{"Failed to send regular message from rank " +
                       std::to_string(current_node_id()) + " to rank " +
                       std::to_string(destination_process_id) +
                       detail::mpi_error_and_message(mpi_result)};
  }
  global_qd_.increment_sends();
}

void DistributedTaskDriver::send_reduction_message_impl(Message_t in_message) {
  // We first combine (or insert) the message. If we get a message back, then
  // we do one of:
  // 1. Invoke callback (only if we have collected everything and are on root
  //    process).
  // 2. Send to parent (if we have everything).
  const std::uint32_t object_index =
      in_message.get_header()->distributed_object_index();
  if (object_index >= distributed_objects_.size()) {
    throw Exception{"The distributed object index is " +
                    std::to_string(object_index) + " but we only have  " +
                    std::to_string(distributed_objects_.size()) +
                    " parallel components."};
  }
  std::optional<Message_t> message =
      distributed_objects_[object_index]
          .reduction_handler->combine_inter_process(std::move(in_message),
                                                    parent_and_children_);
  if (not message.has_value()) {
    return;
  }

  if (message.value().get_header()->data_was_serialized()) {
    throw Exception{"Can't handle serialized data in reductions."};
  }

  if (current_node_id() == 0) {
    // Invoke the callback, but semantics depend on type of callback.
    const reduction::detail::ReductionCallbackImpl callback_impl =
        *reduction::get_callback<reduction::detail::ReductionCallbackImpl>(
            message.value());
    if (callback_impl.message_type_ == MessageType::Invoke) {
      if (callback_impl.distributed_object_index_ >=
          distributed_objects_.size()) {
        throw Exception{
            "Can't retrieve distributed object with index " +
            std::to_string(callback_impl.distributed_object_index_) +
            " in reduction Invoke broadcast because we have " +
            std::to_string(distributed_objects_.size()) + " total objects."};
      }
      const auto& objects =
          distributed_objects_[callback_impl.distributed_object_index_].objects;
      const int target_process =
          objects.index() == detail::DistributedObjectIndex::Collection
              ? std::get<1>(objects)
                    .at(callback_impl.collection_index_)
                    .process_id
              : static_cast<int>(callback_impl.collection_index_);
      Message_t msg = create_message(
          message.value().get_header()->member_function_ptr(),
          objects.index() == detail::DistributedObjectIndex::Collection
              ? callback_impl.collection_index_
              : MessageHeader::no_collection_index(),
          callback_impl.distributed_object_index_, current_node_id(),
          target_process, global_qd_.local_sweep_number(), false,
          rts::MessageType::Invoke,
          message.value().get_header()->data_alignment(),
          reduction::get_data_size(message.value()),
          reduction::get_data_pointer(message.value()));
      send_data(target_process, std::move(msg));
      return;
    } else if (callback_impl.message_type_ == MessageType::Broadcast) {
      Message_t msg = create_message(
          message.value().get_header()->member_function_ptr(),
          MessageHeader::no_collection_index(),
          callback_impl.distributed_object_index_, current_node_id(),
          // For broadcasts we first set the target process ID to
          // self, then update it as we send to different processes.
          current_node_id(), global_qd_.local_sweep_number(), false,
          rts::MessageType::Broadcast,
          message.value().get_header()->data_alignment(),
          reduction::get_data_size(message.value()),
          reduction::get_data_pointer(message.value()));
      send_data(broadcast_process_id, std::move(msg));
      return;
    } else {
      throw Exception{"Don't know how to send callback type " +
                      detail::get_output(callback_impl.message_type_)};
    }
  }
  // Set the sweep number and then initiate the inter-process send. At this
  // stage the `message` is fully configured for transmission up the virtual
  // spanning tree.
  message.value().get_header()->quiescence_detection_sweep_number(
      global_qd_.local_sweep_number());
  send_message_impl(std::move(message.value()));
}

void DistributedTaskDriver::send_to_children(const Message_t& message) {
  const int left = parent_and_children_.left_process_id;
  const int right = parent_and_children_.right_process_id;
  if (left != -1 and right != -1 and left == right) {
    throw Exception("Left and right children are the same process ID: " +
                    std::to_string(left));
  }

  for (const int child : {left, right}) {
    if (child != -1) {
      Message_t child_copy = copy(message);
      child_copy.get_header()->change_destination_process_id(child);
      send_message_impl(std::move(child_copy));
    }
  }
}

void DistributedTaskDriver::initiate_sends(const int max_to_send) {
  if (max_to_send <= 0) {
    throw Exception("max_to_send must be positive but is " +
                    std::to_string(max_to_send));
  }
  std::array<std::tuple<int, Message_t>, 10> bulk_outgoing_messages{};
  for (int i = 0; i < max_to_send; ++i) {
    const size_t messages_retrieved = outgoing_messages_.try_dequeue_bulk(
        bulk_outgoing_messages.data(),
        std::min(bulk_outgoing_messages.size(),
                 static_cast<size_t>(max_to_send)));
    if (messages_retrieved == 0) {
      break;
    }
    for (size_t to_send = 0; to_send < messages_retrieved; ++to_send) {
      if (const MessageHeader& msg_hdr =
              *std::get<1>(bulk_outgoing_messages[to_send]).get_header();
          msg_hdr.is_broadcast()) {
        Message_t& message = std::get<1>(bulk_outgoing_messages[to_send]);
        MessageHeader& message_header = *message.get_header();
        // If we are not on process 0 we send to process 0 which starts the
        // tree-based broadcast to its children.
        if (message_header.destination_process_id() == current_node_id() and
            message_header.source_process_id() == current_node_id() and
            current_node_id() != 0) {
          message_header.change_destination_process_id(0);
          send_message_impl(std::move(message));
          continue;
        }
        // Perform tree-based broadcast to children.
        send_to_children(message);

        // Send to local collection elements.
        if (message_header.distributed_object_index() >=
            distributed_objects_.size()) {
          throw Exception{
              "The distributed object index in the message, " +
              std::to_string(message_header.distributed_object_index()) +
              ", is out of range. Maximum index is " +
              std::to_string(distributed_objects_.size() - 1)};
        }
        auto& distributed_object =
            distributed_objects_[message_header.distributed_object_index()];

        std::vector<Message_t> all_local_messages{};
        all_local_messages.reserve(
            static_cast<size_t>(distributed_object.number_of_local_objects));

        add_local_broadcast_tasks(all_local_messages, message);
        thread_pool_->add_tasks(
            std::make_move_iterator(all_local_messages.begin()),
            all_local_messages.size());
      } else if (msg_hdr.is_broadcast_to()) {
        send_message_impl(
            std::move(std::get<1>(bulk_outgoing_messages[to_send])));
      } else if (msg_hdr.message_type() == MessageType::Invoke) {
        send_message_impl(
            std::move(std::get<1>(bulk_outgoing_messages[to_send])));
      } else if (msg_hdr.message_type() == MessageType::Reduction or
                 msg_hdr.message_type() == MessageType::ReductionOver) {
        send_reduction_message_impl(
            std::move(std::get<1>(bulk_outgoing_messages[to_send])));
      } else {
        throw Exception{
            "Don't know how to handle message type in initiate_sends " +
            detail::get_output(std::get<1>(bulk_outgoing_messages[to_send])
                                   .get_header()
                                   ->message_type())};
      }
    }
    i += messages_retrieved;
  }
}

void DistributedTaskDriver::clean_outgoing_mpi_messages() {
  auto erase_start = std::remove_if(
      outgoing_mpi_messages_.begin(), outgoing_mpi_messages_.end(),
      [this](std::tuple<std::optional<MPI_Request>, Message_t>& elem) {
        if (not std::get<0>(elem).has_value()) {
          throw Exception{
              "Trying to clear an outgoing MPI message on process " +
              std::to_string(current_node_id()) +
              " with an empty request. This is a bug so please file an issue "
              "with a minimal reproducible example."};
        }
        int flag{0};
        if (const auto mpi_result = MPI_Request_get_status(
                std::get<0>(elem).value(), &flag, MPI_STATUS_IGNORE);
            mpi_result != MPI_SUCCESS) {
          throw MpiException{"Failed to get status of a regular message."};
        }
        if (static_cast<bool>(flag)) {
          if (const auto mpi_result =
                  MPI_Request_free(&std::get<0>(elem).value());
              mpi_result != MPI_SUCCESS) {
            throw MpiException{
                "Failed to call MPI_Request_free on a regular message."};
          }
          std::get<0>(elem) = std::nullopt;
          return true;
        }
        return false;
      });
  outgoing_mpi_messages_.erase(erase_start, outgoing_mpi_messages_.end());
}

void DistributedTaskDriver::initiate_receives(const int max_to_receive) {
  if (max_to_receive <= 0) {
    throw Exception("max_to_send must be positive but is " +
                    std::to_string(max_to_receive));
  }
  for (int number_of_receives = 0; number_of_receives < max_to_receive;
       ++number_of_receives) {
    if (node_id_for_receive_ >= number_of_nodes()) {
      node_id_for_receive_ = 0;
    }
    if (node_id_for_receive_ == current_node_id()) {
      ++node_id_for_receive_;
      continue;
    }
    int flag{0};
    MPI_Status status{};
    if (const auto mpi_result =
            MPI_Iprobe(node_id_for_receive_, message_tags::regular, rts_comm_,
                       &flag, &status);
        mpi_result != MPI_SUCCESS) {
      throw MpiException{
          "Failed to call MPI_Iprobe_to check for a regular "
          "message."};
    }
    if (not static_cast<bool>(flag)) {
      // Received nothing from this node, check the next node.
      ++node_id_for_receive_;
      continue;
    }
    if (status.MPI_ERROR != MPI_SUCCESS) {
      throw MpiException{
          "Failed to receive a regular message because the "
          "MPI_Status.MPI_ERROR field is not MPI_SUCCESS."};
    }
    int message_size = 0;
    if (const auto mpi_result =
            MPI_Get_elements(&status, MPI_BYTE, &message_size);
        mpi_result != MPI_SUCCESS) {
      throw MpiException{
          "Failed to retrieve the size of the incoming regular message using "
          "MPI_Get_elements. Error code: " +
          std::to_string(mpi_result)};
    }
    if (message_size < 0) {
      throw Exception{"Received a negative message size, " +
                      std::to_string(message_size)};
    }
    incoming_mpi_messages_.emplace_back(
        MPI_Request{},
        Message_t{std::unique_ptr<std::byte[]>{
            new std::byte[static_cast<unsigned long>(message_size)]}});
    auto& [request, message] = incoming_mpi_messages_.back();
    MPI_Irecv(message.message.get(), message_size, MPI_BYTE,
              node_id_for_receive_, message_tags::regular, rts_comm_, &request);
    ++node_id_for_receive_;
  }
}

namespace {
/*
 * Iterator that wraps the iterator for
 * `DistributedTaskDriver::OutgoingMpiMessages_t` in order to enable bulk
 * enqueue of messages. The queue we are currently using only needs a handful
 * of operators defined.
 */
struct BulkEnqueueIterator {
  BulkEnqueueIterator(
      typename DistributedTaskDriver::IncomingMpiMessages_t::iterator it_in)
      : it(std::move(it_in)) {}
  BulkEnqueueIterator(const BulkEnqueueIterator&) = default;
  BulkEnqueueIterator& operator=(const BulkEnqueueIterator&) = default;
  BulkEnqueueIterator(BulkEnqueueIterator&&) = default;
  BulkEnqueueIterator& operator=(BulkEnqueueIterator&&) = default;
  ~BulkEnqueueIterator() = default;

  Message_t operator*() {
    if (already_dereferenced) {
      throw Exception{
          "Already dereferenced the iterator and we can only dereference it "
          "once."};
    }
    already_dereferenced = true;
    if (const MessageType message_type =
            std::get<1>(*it).get_header()->message_type();
        message_type != MessageType::Invoke) {
      throw Exception{
          "The received message type must be Invoke. Other message types need "
          "to be preprocessed and turned into Invoke messages. The message "
          "type received is " +
          detail::get_output(message_type)};
    }
    return std::move(std::get<1>(*it));
  }

  BulkEnqueueIterator& operator++() {
    ++it;
    return *this;
  }

  BulkEnqueueIterator operator++(int) {
    const auto ret = *this;
    operator++();
    return ret;
  }

  bool already_dereferenced{false};
  typename DistributedTaskDriver::IncomingMpiMessages_t::iterator it;
};
}  // namespace

void DistributedTaskDriver::clean_incoming_mpi_messages() {
  auto first_received_message = std::remove_if(
      incoming_mpi_messages_.begin(), incoming_mpi_messages_.end(),
      [](std::tuple<MPI_Request, Message_t>& elem) {
        int flag{0};
        if (const auto mpi_result = MPI_Request_get_status(
                std::get<0>(elem), &flag, MPI_STATUS_IGNORE);
            mpi_result != MPI_SUCCESS) {
          throw MpiException{"Failed to get status of a regular message."};
        }
        if (static_cast<bool>(flag)) {
          return true;
        }
        return false;
      });
  const auto messages_to_emplace =
      std::distance(first_received_message, incoming_mpi_messages_.end());
  if (messages_to_emplace == 0) {
    return;
  } else if (messages_to_emplace < 0) {
    throw Exception(
        "The messages to emplace should be non-negative. This is an internal "
        "bug.");
  }
  int number_of_message_to_enqueue = 0;
  for (auto it = first_received_message; it != incoming_mpi_messages_.end();
       ++it) {
    global_qd_.increment_processed();
    Message_t& message = std::get<1>(*it);
    MessageHeader& message_header = *message.get_header();
    global_qd_.update_last_regular_message_sweep_number(
        message_header.quiescence_detection_sweep_number());
    MPI_Request_free(&std::get<0>(*it));
    if (message_header.is_broadcast()) {
      send_to_children(message);
    }

    // Count number of messages to enqueue.
    // Invoke: 1
    // Broadcast: total local collection objects
    // BroadcastTo: number of collection elements in message
    if (message_header.message_type() == MessageType::Invoke) {
      ++number_of_message_to_enqueue;
    } else if (message_header.is_broadcast() or
               message_header.is_broadcast_to()) {
      // For all message types other than BroadcastTo we send
      // distributed_object.number_of_local_objects number of local messages.
      const auto dist_object_index = message_header.distributed_object_index();

      // Check if this is a collection or a non-collection parallel component
      if (dist_object_index >= distributed_objects_.size()) {
        throw Exception{"The distributed object index " +
                        std::to_string(dist_object_index) +
                        " is out of range. We have " +
                        std::to_string(distributed_objects_.size()) +
                        " total parallel components/distributed objects."};
      }
      const DistributedOjectClassHolder& distributed_object =
          distributed_objects_[dist_object_index];
      if (distributed_object.number_of_local_objects == -1) {
        throw Exception{"The number of local objects for parallel component " +
                        distributed_object.name + " on process ID " +
                        std::to_string(current_node_id()) +
                        " is -1. This is an internal bug. The number of local "
                        "objects must be non-negative."};
      }
      if (message_header.is_broadcast()) {
        number_of_message_to_enqueue +=
            distributed_object.number_of_local_objects;
      } else {
        const std::uint64_t* start =
            reinterpret_cast<const std::uint64_t*>(std::next(&message_header));
        if (*start != static_cast<std::uint64_t>(current_node_id())) {
          throw Exception{
              "Received BroadcastTo message for process ID " +
              std::to_string(*start) + " but on process " +
              std::to_string(current_node_id()) +
              ". This is an internal bug. Please file an issue with "
              "a minimal reproducible example."};
        }
        if (*std::next(start) >
            static_cast<std::uint64_t>(
                distributed_object.number_of_local_objects)) {
          throw Exception{
              "Received BroadcastTo with " + std::to_string(*std::next(start)) +
              " elements but we have " +
              std::to_string(distributed_object.number_of_local_objects) +
              " local objects."};
        }
        number_of_message_to_enqueue += *std::next(start);
      }
    } else if (message_header.message_type() == MessageType::Reduction or
               message_header.message_type() == MessageType::ReductionOver) {
      send_reduction_message_impl(std::move(message));
    } else {
      throw Exception{detail::get_output(message_header.message_type()) +
                      " messages not yet supported in "
                      "clean_incoming_mpi_messages()."};
    }
  }

  if (number_of_message_to_enqueue < 0) {
    throw Exception{
        "The number_of_message_to_enqueue must be non-negative but it is " +
        std::to_string(number_of_message_to_enqueue)};
  }

  // Note: We could combine this computation of `all_messages_are_invoke` with
  // the preceding for loop that counts the number of messages to enqueue.
  // However, that loop already contains multiple conditional branches and is
  // fairly complex, so separating this logic improves readability and
  // maintainability. If the first loop is simplified later, we could consider
  // merging these steps for efficiency.
  const bool all_messages_are_invoke =
      std::all_of(first_received_message, incoming_mpi_messages_.end(),
                  [](const std::tuple<MPI_Request, Message_t>& msg) {
                    // Reduction(Over) messages are null, so skip those.
                    return std::get<1>(msg).message != nullptr and
                           std::get<1>(msg).get_header()->message_type() ==
                               MessageType::Invoke;
                  });

  if (all_messages_are_invoke) {
    thread_pool_->add_tasks(BulkEnqueueIterator{first_received_message},
                            static_cast<size_t>(messages_to_emplace));
  } else {
    // Since we have some broadcast messages, we need to move the messages
    // into a separate buffer before adding the tasks.
    //
    // Note: we may have fewer than the total number of messages because a
    // process may have no collection elements on it and so the broadcast
    // would not have any local tasks.
    std::vector<Message_t> all_tasks{};
    all_tasks.reserve(static_cast<size_t>(number_of_message_to_enqueue));
    for (auto it = first_received_message; it != incoming_mpi_messages_.end();
         ++it) {
      Message_t& msg = std::get<1>(*it);
      if (msg.message == nullptr) {
        // Reduction(Over) messages are null, so skip those.
        continue;
      }
      MessageHeader* header = msg.get_header();

      if (header->message_type() == MessageType::Invoke) {
        all_tasks.push_back(std::move(msg));
      } else if (header->is_broadcast() or header->is_broadcast_to()) {
        add_local_broadcast_tasks(all_tasks, msg);
      } else {
        throw Exception{
            "Unsupported message type in clean_incoming_mpi_messages(). "
            "Received message type is " +
            detail::get_output(header->message_type())};
      }
    }

    thread_pool_->add_tasks(std::make_move_iterator(all_tasks.begin()),
                            all_tasks.size());
  }
  incoming_mpi_messages_.erase(first_received_message,
                               incoming_mpi_messages_.end());
}

void DistributedTaskDriver::add_local_broadcast_tasks(
    std::vector<Message_t>& all_tasks, const Message_t& message) const {
  const MessageHeader& message_header = *message.get_header();
  // Send to local collection elements.
  if (message_header.distributed_object_index() >=
      distributed_objects_.size()) {
    throw Exception{"The distributed object index in the message, " +
                    std::to_string(message_header.distributed_object_index()) +
                    ", is out of range. Maximum index is " +
                    std::to_string(distributed_objects_.size() - 1)};
  }
  const DistributedOjectClassHolder& distributed_object =
      distributed_objects_[message_header.distributed_object_index()];
  const DistributedOjectClassHolder::variant_t& objects_variant =
      distributed_object.objects;
  if (objects_variant.index() == detail::Collection) {
    if (message.get_header()->is_broadcast()) {
      const DistributedOjectClassHolder::Map_t& objects =
          std::get<1>(objects_variant);
      // For each local collection element, create a copy and update header
      int local_send_counter = 0;
      for (const auto& [collection_index, collection_holder] : objects) {
        if (local_send_counter >= distributed_object.number_of_local_objects) {
          break;
        }
        if (collection_holder.process_id != current_node_id()) {
          continue;
        }

        Message_t local_message = copy(message);
        MessageHeader* local_header = local_message.get_header();

        // Change message type to Invoke and set collection index
        local_header->convert_broadcast_to_invoke(collection_index);
        local_header->change_destination_process_id(current_node_id());
        all_tasks.push_back(std::move(local_message));
        ++local_send_counter;
      }
    } else if (message.get_header()->is_broadcast_to()) {
      const DistributedOjectClassHolder::Map_t& objects =
          std::get<1>(objects_variant);

      const std::uint64_t* start = reinterpret_cast<const std::uint64_t*>(
          std::next(message.get_header()));
      if (*start != static_cast<std::uint64_t>(current_node_id())) {
        throw Exception{
            "The target process ID read from the BroadcastTo buffer is " +
            std::to_string(*start) + " but we are on process ID " +
            std::to_string(current_node_id()) +
            ". This is an internal bug. Please file an issue with a minimal "
            "reproducible example."};
      }
      const MessageHeader& header = *message.get_header();
      const int number_of_local_elements = static_cast<int>(*std::next(start));
      const std::uint64_t data_size =
          header.number_of_bytes_in_message() - header.data_offset();
      for (int i = 0; i < number_of_local_elements; ++i) {
        const std::uint64_t collection_index = *std::next(start, 2 + i);
        if (const auto it = objects.find(collection_index);
            it == objects.end()) {
          throw Exception{"The collection index " +
                          std::to_string(collection_index) +
                          " does not exist for the collection. Current element "
                          "iteration index " +
                          std::to_string(i) + " and number of elements " +
                          std::to_string(number_of_local_elements)};
        }
        all_tasks.push_back(create_message(
            message_header.member_function_ptr(), collection_index,
            message_header.distributed_object_index(),
            message_header.source_process_id(), current_node_id(),
            message_header.quiescence_detection_sweep_number(),
            message_header.data_was_serialized(), rts::MessageType::Invoke,
            message_header.data_alignment(), data_size,
            header.data_location()));
      }
    } else {
      throw Exception{
          "Only Broadcast and BroadcastTo messages are support in "
          "add_local_broadcast_tasks but got message of type " +
          detail::get_output(message.get_header()->message_type())};
    }
  } else {
    if (objects_variant.index() != detail::Regular) {
      throw Exception{"Unsupported variant index " +
                      std::to_string(objects_variant.index()) +
                      ". We only support Collection and Regular components in "
                      "broadcasts currently."};
    }
    Message_t local_message = copy(message);
    MessageHeader* local_header = local_message.get_header();

    // Change message type to Invoke and set collection index
    local_header->convert_broadcast_to_invoke(
        MessageHeader::no_collection_index());
    local_header->change_destination_process_id(current_node_id());
    all_tasks.push_back(std::move(local_message));
  }
}

DistributedTaskDriver::DistributedOjectClassHolder::DistributedOjectClassHolder(
    std::unique_ptr<detail::DistributedObjectBase> in_object,
    std::string in_name, const size_t number_of_threads,
    const size_t max_simultaneous_reductions)
    : objects(std::move(in_object)),
      name(std::move(in_name)),
      reduction_handler(std::make_unique<reduction::Handler>(
          number_of_threads, max_simultaneous_reductions)) {}

DistributedTaskDriver::DistributedOjectClassHolder::DistributedOjectClassHolder(
    std::unordered_map<uint64_t, CollectionHolder> in_objects,
    std::string in_name, const size_t number_of_processes,
    const size_t number_of_threads, const size_t max_simultaneous_reductions)
    : objects(std::move(in_objects)),
      ids_per_process(number_of_processes),
      name(std::move(in_name)),
      reduction_handler(std::make_unique<reduction::Handler>(
          number_of_threads, max_simultaneous_reductions)) {}

thread_local std::uint32_t DistributedTaskDriver::thread_id_ =
    std::numeric_limits<std::uint32_t>::max();

static const std::unique_ptr<DistributedTaskDriver> task_driver = nullptr;

DistributedTaskDriver& create_distributed_task_driver(
    int* argc, char** argv[], const bool initialize_mpi) {
  if (task_driver != nullptr) {
    throw Exception(
        "Already initialized the task driver. You should only initialize the "
        "driver once.");
  }
  int mpi_threading_support = MPI_THREAD_SINGLE;
  if (initialize_mpi) {
    if (argc == nullptr or argv == nullptr) {
      throw Exception{
          "Either argc or argv is nullptr. We cannot initialize MPI with them "
          "being nullptrs."};
    }
    if (MPI_Init_thread(argc, argv, MPI_THREAD_MULTIPLE,
                        &mpi_threading_support) != MPI_SUCCESS) {
      throw MpiException("Failed to initialize MPI");
    }
  }

  // If we initialize MPI then we also finalize it on destruction.
  const_cast<std::unique_ptr<DistributedTaskDriver>&>(task_driver) =
      std::unique_ptr<DistributedTaskDriver>(new DistributedTaskDriver(
          initialize_mpi, mpi_threading_support == MPI_THREAD_MULTIPLE));
  task_driver->attach_debugger();
  detail::print_process_pids(task_driver->current_node_id());
  task_driver->barrier();
  return *task_driver.get();
}
}  // namespace rts

// Callback.cpp equivalent
//
// We define the functions here since we need access to the global
// DistributedTaskDriver object, and also because MPI-based tests have
// significant 0.5 second or more startup overhead, so combining them helps
// keep test runtime down.
namespace rts {
CallbackBase::CallbackBase() = default;
CallbackBase::~CallbackBase() = default;

DistributedTaskDriver& CallbackBase::get_task_driver() const {
  if (task_driver == nullptr) {
    throw Exception{
        "The task_driver has not been set yet. You must first call "
        "create_distributed_task_driver()"};
  }
  return *task_driver;
}
}  // namespace rts

#if defined(RTS_ENABLE_TESTING)

#include <cstdint>
#include <doctest/doctest.h>
#include <doctest/extensions/doctest_mpi.h>
#include <mutex>
#include <set>
#include <unordered_set>
#include <vector>

#include "Rts/Detail/IndexConversion.hpp"
#include "Rts/DistributedObjectCollection.hpp"
#include "Rts/Serialize/Stl/Vector.hpp"

namespace rts {
namespace {
namespace testing {
void test_bulk_enequeue_iterator_exceptions() {
  using IncomingMpiMessages_t = DistributedTaskDriver::IncomingMpiMessages_t;

  // Helper to create a Message_t with a given MessageType
  auto make_message = [](rts::MessageType type) -> Message_t {
    const std::uint64_t num_bytes = sizeof(rts::MessageHeader);
    std::unique_ptr<std::byte[]> buffer(new std::byte[num_bytes]);
    new (buffer.get())
        rts::MessageHeader(rts::detail::MemberFunctionPtr{}, 0, num_bytes, 0, 0,
                           0, 0, 0, false, type);
    return {std::move(buffer)};
  };

  {
    // 1. Test already dereferenced exception
    IncomingMpiMessages_t vec{};
    vec.emplace_back(MPI_Request{}, make_message(rts::MessageType::Invoke));
    BulkEnqueueIterator bulk_it(vec.begin());
    // First dereference should succeed
    CHECK_NOTHROW(*bulk_it);
    // Second dereference should throw
    CHECK_THROWS_WITH_AS(*bulk_it,
                         "Already dereferenced the iterator and we can only "
                         "dereference it once.",
                         rts::Exception);
  }

  {
    // 2. Test message type not Invoke exception
    IncomingMpiMessages_t vec{};
    vec.emplace_back(MPI_Request{}, make_message(rts::MessageType::Broadcast));
    BulkEnqueueIterator bulk_it(vec.begin());
    CHECK_THROWS_WITH_AS(
        *bulk_it,
        "The received message type must be Invoke. Other message types need to "
        "be preprocessed and turned into Invoke messages. The message type "
        "received is Broadcast",
        rts::Exception);
  }
}

// Action tags for clarity
struct TestAction {};
struct TestBroadcastAction {};
struct TestBroadcastToAction {};

// Regular parallel component
struct RegularComponent : public rts::detail::DistributedObjectBase {
  int last_result = 0;
  std::tuple<int, int> last_args{0, 0};
  std::tuple<int, int, double> last_broadcast_args{0, 0, 0.0};

  static std::string name() { return "RegularComponent"; }

  template <class Action, class... Args>
  void threaded_action(rts::DistributedTaskDriver&, Args... args);

  template <>
  void threaded_action<TestAction>(rts::DistributedTaskDriver&,
                                   const int a, const int b) {
    last_args = std::make_tuple(a, b);
    last_result += static_cast<int>(a + b);
  }

  template <>
  void threaded_action<TestAction>(rts::DistributedTaskDriver&,
                                   const std::vector<int> a) {
    last_args =
        std::tuple{static_cast<int>(a.size()), static_cast<int>(a.capacity())};
    last_result = std::accumulate(a.begin(), a.end(), 0);
  }

  // Specialization for broadcast (int, int, double)
  template <>
  void threaded_action<TestBroadcastAction>(rts::DistributedTaskDriver&,
                                            const int a, const int b,
                                            const double d) {
    last_broadcast_args = std::make_tuple(a, b, d);
    last_result = static_cast<int>(a + b + d);
  }

  // Specialization for broadcast (int, int, double, std::vector<int>)
  template <>
  void threaded_action<TestBroadcastAction>(rts::DistributedTaskDriver&,
                                            const int a, const int b,
                                            const double d,
                                            const std::vector<int> vec) {
    last_broadcast_args = std::make_tuple(a, b, d);
    last_result = static_cast<int>(a + b + d) +
                  std::accumulate(vec.begin(), vec.end(), 0);
  }
};

// Collection parallel component
struct CollectionComponent
    : public rts::DistributedObjectCollection<CollectionComponent> {
  using rts_collection_index = uint64_t;
  int last_result{0};
  std::tuple<int, int> last_args{};
  std::tuple<int, int, double> last_broadcast_args{0, 0, 0.0};
  std::tuple<int, double> last_broadcast_to_args{0, 0.0};

  static std::string name() { return "CollectionComponent"; }

  template <class Action, class... Args>
  void threaded_action(rts::DistributedTaskDriver&,
                       rts_collection_index my_index, Args... args);

  template <>
  void threaded_action<TestAction>(rts::DistributedTaskDriver&,
                                   const rts_collection_index my_index,
                                   const int a, const int b) {
    CHECK(my_index != 0);
    CHECK(my_index != MessageHeader::no_collection_index());
    last_args = std::make_tuple(a, b);
    last_result += static_cast<int>(a + b);
  }

  template <>
  void threaded_action<TestAction>(rts::DistributedTaskDriver&,
                                   const rts_collection_index my_index,
                                   const std::vector<int> a) {
    CHECK(my_index != 0);
    CHECK(my_index != MessageHeader::no_collection_index());
    last_args =
        std::tuple{static_cast<int>(a.size()), static_cast<int>(a.capacity())};
    last_result += std::accumulate(a.begin(), a.end(), 0);
  }

  // Specialization for broadcast (int, int, double)
  template <>
  void threaded_action<TestBroadcastAction>(rts::DistributedTaskDriver&,
                                            const rts_collection_index my_index,
                                            const int a, const int b,
                                            const double d) {
    CHECK(my_index != 0);
    CHECK(my_index != MessageHeader::no_collection_index());
    last_broadcast_args = std::make_tuple(a, b, d);
    last_result = static_cast<int>(a + b + d);
  }

  // Specialization for broadcast (int, int, double, std::vector<int>)
  template <>
  void threaded_action<TestBroadcastAction>(rts::DistributedTaskDriver&,
                                            const rts_collection_index my_index,
                                            const int a, const int b,
                                            const double d,
                                            const std::vector<int> vec) {
    CHECK(my_index != 0);
    CHECK(my_index != MessageHeader::no_collection_index());
    last_broadcast_args = std::make_tuple(a, b, d);
    last_result = static_cast<int>(a + b + d) +
                  std::accumulate(vec.begin(), vec.end(), 0);
  }

  // Specialization for broadcast_to (int, double)
  template <>
  void threaded_action<TestBroadcastToAction>(
      rts::DistributedTaskDriver&, const rts_collection_index my_index,
      const int a, const double d) {
    CHECK(my_index != 0);
    CHECK(my_index != MessageHeader::no_collection_index());
    last_broadcast_to_args = std::make_tuple(a, d);
    last_result = static_cast<int>(a + d);
  }

  // Specialization for broadcast_to (int, double, std::vector<int>)
  template <>
  void threaded_action<TestBroadcastToAction>(
      rts::DistributedTaskDriver&, const rts_collection_index my_index,
      const int a, const double d, const std::vector<int> vec) {
    CHECK(my_index != 0);
    CHECK(my_index != MessageHeader::no_collection_index());
    last_broadcast_to_args = std::make_tuple(a, d);
    last_result =
        static_cast<int>(a + d) + std::accumulate(vec.begin(), vec.end(), 0);
  }
};

void reset_args_on_all(DistributedTaskDriver& driver) {
  driver.insert_barrier();
  RegularComponent* const reg =
      rts::local_parallel_component<RegularComponent>(driver);
  reg->last_result = 0;
  reg->last_args = std::tuple<int, int>{0, 0};
  reg->last_broadcast_args = std::tuple<int, int, double>{0, 0, 0.0};

  for (const auto& [idx, holder] :
       driver.collection_ids_and_locations<CollectionComponent>()) {
    auto* const elem =
        rts::local_parallel_component<CollectionComponent>(driver, idx);
    if (elem != nullptr) {
      CHECK(holder.process_id == driver.current_node_id());
      elem->last_result = 0;
      elem->last_args = std::make_tuple(0, 0.0);
      elem->last_broadcast_args = std::make_tuple(0, 0, 0.0);
      elem->last_broadcast_to_args = std::make_tuple(0, 0.0);
    } else {
      CHECK(holder.process_id != driver.current_node_id());
    }
  }

  driver.insert_barrier();
}

void test_callbacks(DistributedTaskDriver& driver) {
  const int number_of_processes = driver.number_of_nodes();

  reset_args_on_all(driver);

  driver.insert_barrier();
  driver.launch_threads();

  auto test_invoke = [&](const int from_process) {
    std::unique_ptr<CallbackBase> cb{nullptr};
    std::unique_ptr<CallbackBase> cb_clone{nullptr};
    if (driver.current_node_id() == from_process) {
      // Element 42ul was inserted in test_invoke.
      cb = rts::make_unique_invoke_callback<TestAction, CollectionComponent>(
          42ul, 7 + from_process, 13);

      CHECK_FALSE(cb->was_invoked());
      CHECK(cb->name().find("CallbackInvoke") != std::string::npos);
      CHECK(cb->name().find("CollectionComponent") != std::string::npos);

      cb_clone = cb->get_clone();
      CHECK(cb->is_equal_to(*cb_clone));

      cb->invoke();
      CHECK(cb->was_invoked());
    }

    driver.run_to_quiescence();

    if (driver.current_node_id() == 0) {
      auto* const elem =
          rts::local_parallel_component<CollectionComponent>(driver, 42ul);
      CHECK(elem->last_args == std::tuple{7 + from_process, 13});
      CHECK(elem->last_broadcast_args == std::make_tuple(0, 0, 0.0));
      CHECK(elem->last_broadcast_to_args == std::make_tuple(0, 0.0));
      elem->last_args = std::tuple{0, 0};
    }

    if (driver.current_node_id() == from_process) {
      CHECK_THROWS_WITH_AS(
          cb->invoke(),
          "Already invoked the Callback. Cannot invoke() it a second time. You "
          "must first make a copy of the callback, for example using "
          "get_clone(), and then call invoke() on the clone the second time.",
          rts::Exception);

      // Note: because the types being sent (ints) are trivially copyable, the
      // move semantics decay to copy and so are clone is equal to the
      CHECK(cb->is_equal_to(*cb_clone));
      CHECK(cb_clone->name() == cb->name());
      CHECK_FALSE(cb_clone->was_invoked());
    }
    driver.insert_barrier();

    if (driver.current_node_id() == from_process) {
      cb_clone->invoke();
    }

    driver.run_to_quiescence();

    if (driver.current_node_id() == from_process) {
      CHECK(cb_clone->was_invoked());
    }

    if (driver.current_node_id() == 0) {
      auto* const elem =
          rts::local_parallel_component<CollectionComponent>(driver, 42ul);
      CHECK(elem->last_args == std::tuple{7 + from_process, 13});
      CHECK(elem->last_broadcast_args == std::make_tuple(0, 0, 0.0));
      CHECK(elem->last_broadcast_to_args == std::make_tuple(0, 0.0));
      elem->last_args = std::tuple{0, 0};
    }
    driver.barrier();
  };

  for (int from_pid = 0; from_pid < number_of_processes; ++from_pid) {
    test_invoke(from_pid);
    reset_args_on_all(driver);
  }

  auto test_broadcast = [&](const int from_process) {
    std::unique_ptr<CallbackBase> cb{nullptr};
    std::unique_ptr<CallbackBase> cb_clone{nullptr};
    if (driver.current_node_id() == from_process) {
      // Element 42ul was inserted in test_invoke.
      cb = rts::make_unique_broadcast_callback<TestBroadcastAction,
                                               CollectionComponent>(
          9 + from_process, 23, 2.3);

      CHECK_FALSE(cb->was_invoked());
      CHECK(cb->name().find("CallbackBroadcast") != std::string::npos);
      CHECK(cb->name().find("CollectionComponent") != std::string::npos);

      cb_clone = cb->get_clone();
      CHECK(cb->is_equal_to(*cb_clone));

      cb->invoke();
      CHECK(cb->was_invoked());
    }

    driver.run_to_quiescence();

    for (const auto& [idx, holder] :
         driver.collection_ids_and_locations<CollectionComponent>()) {
      auto* const elem =
          rts::local_parallel_component<CollectionComponent>(driver, idx);
      if (elem != nullptr) {
        CHECK(holder.process_id == driver.current_node_id());
        CHECK(elem->last_args == std::make_tuple(0, 0.0));
        CHECK(elem->last_broadcast_args ==
              std::tuple{9 + from_process, 23, 2.3});
        CHECK(elem->last_broadcast_to_args == std::make_tuple(0, 0.0));
      } else {
        CHECK(holder.process_id != driver.current_node_id());
      }
    }

    if (driver.current_node_id() == from_process) {
      CHECK_THROWS_WITH_AS(
          cb->invoke(),
          "Already invoked the Callback. Cannot invoke() it a second time. You "
          "must first make a copy of the callback, for example using "
          "get_clone(), and then call invoke() on the clone the second time.",
          rts::Exception);

      // Note: because the types being sent (ints) are trivially copyable, the
      // move semantics decay to copy and so are clone is equal to the
      CHECK(cb->is_equal_to(*cb_clone));
      CHECK(cb_clone->name() == cb->name());
      CHECK_FALSE(cb_clone->was_invoked());
    }
    driver.insert_barrier();

    if (driver.current_node_id() == from_process) {
      cb_clone->invoke();
    }

    driver.run_to_quiescence();

    if (driver.current_node_id() == from_process) {
      CHECK(cb_clone->was_invoked());
    }

    for (const auto& [idx, holder] :
         driver.collection_ids_and_locations<CollectionComponent>()) {
      auto* const elem =
          rts::local_parallel_component<CollectionComponent>(driver, idx);
      if (elem != nullptr) {
        CHECK(holder.process_id == driver.current_node_id());
        CHECK(elem->last_args == std::make_tuple(0, 0.0));
        CHECK(elem->last_broadcast_args ==
              std::tuple{9 + from_process, 23, 2.3});
        CHECK(elem->last_broadcast_to_args == std::make_tuple(0, 0.0));
      } else {
        CHECK(holder.process_id != driver.current_node_id());
      }
    }
    driver.barrier();
  };

  for (int from_pid = 0; from_pid < number_of_processes; ++from_pid) {
    test_broadcast(from_pid);
    reset_args_on_all(driver);
  }

  auto test_broadcast_to = [&](const int from_process, const bool check_even) {
    const auto predicate = [&check_even](const uint64_t index) {
      return index % 2 == (check_even ? 0 : 1);
    };
    std::unique_ptr<CallbackBase> cb{nullptr};
    std::unique_ptr<CallbackBase> cb_clone{nullptr};
    if (driver.current_node_id() == from_process) {
      cb = rts::make_unique_broadcast_to_callback<TestBroadcastToAction,
                                                  CollectionComponent>(
          predicate, 11 + from_process, 7.5);

      CHECK_FALSE(cb->was_invoked());
      CHECK(cb->name().find("CallbackBroadcast") != std::string::npos);
      CHECK(cb->name().find("CollectionComponent") != std::string::npos);

      cb_clone = cb->get_clone();
      CHECK(cb->is_equal_to(*cb_clone));

      cb->invoke();
      CHECK(cb->was_invoked());
    }

    driver.run_to_quiescence();

    for (const auto& [idx, holder] :
         driver.collection_ids_and_locations<CollectionComponent>()) {
      auto* const elem =
          rts::local_parallel_component<CollectionComponent>(driver, idx);
      if (elem != nullptr) {
        CHECK(holder.process_id == driver.current_node_id());
        CHECK(elem->last_args == std::make_tuple(0, 0.0));
        CHECK(elem->last_broadcast_args == std::make_tuple(0, 0, 0.0));
        if (predicate(idx)) {
          CHECK(elem->last_broadcast_to_args ==
                std::tuple{11 + from_process, 7.5});
        } else {
          CHECK(elem->last_broadcast_to_args == std::tuple{0, 0.0});
        }
      } else {
        CHECK(holder.process_id != driver.current_node_id());
      }
    }

    if (driver.current_node_id() == from_process) {
      CHECK_THROWS_WITH_AS(
          cb->invoke(),
          "Already invoked the Callback. Cannot invoke() it a second time. You "
          "must first make a copy of the callback, for example using "
          "get_clone(), and then call invoke() on the clone the second time.",
          rts::Exception);

      // Note: because the types being sent (ints) are trivially copyable, the
      // move semantics decay to copy and so are clone is equal to the
      CHECK(cb->is_equal_to(*cb_clone));
      CHECK(cb_clone->name() == cb->name());
      CHECK_FALSE(cb_clone->was_invoked());
    }
    driver.insert_barrier();

    if (driver.current_node_id() == from_process) {
      cb_clone->invoke();
    }

    driver.run_to_quiescence();

    if (driver.current_node_id() == from_process) {
      CHECK(cb_clone->was_invoked());
    }

    for (const auto& [idx, holder] :
         driver.collection_ids_and_locations<CollectionComponent>()) {
      auto* const elem =
          rts::local_parallel_component<CollectionComponent>(driver, idx);
      if (elem != nullptr) {
        CHECK(holder.process_id == driver.current_node_id());
        CHECK(elem->last_args == std::make_tuple(0, 0.0));
        CHECK(elem->last_broadcast_args == std::make_tuple(0, 0, 0.0));
        if (predicate(idx)) {
          CHECK(elem->last_broadcast_to_args ==
                std::tuple{11 + from_process, 7.5});
        } else {
          CHECK(elem->last_broadcast_to_args == std::tuple{0, 0.0});
        }
      } else {
        CHECK(holder.process_id != driver.current_node_id());
      }
    }
    driver.barrier();
  };

  for (int from_pid = 0; from_pid < number_of_processes; ++from_pid) {
    for (const bool check_even : {true, false}) {
      test_broadcast_to(from_pid, check_even);
      reset_args_on_all(driver);
    }
  }

  driver.force_threads_to_stop();
}

void test_invoke(DistributedTaskDriver& driver) {
  const int number_of_processes = driver.number_of_nodes();
  // Insert regular and collection components
  driver.insert_parallel_component<RegularComponent>();
  const std::unordered_map<uint64_t, int> all_indices{
      {42ul, 0}, {43ul, 1}, {44ul, 1}, {45ul, 1},
      {46ul, 0}, {47ul, 0}, {48ul, 0}};

  for (const auto& [id, pid] : all_indices) {
    driver.insert_parallel_component_collection<CollectionComponent>(id, pid);
  }

  REQUIRE(driver.collection_ids_and_locations<CollectionComponent>().size() ==
          all_indices.size());
  REQUIRE(driver.collection_ids_on_processes<CollectionComponent>().size() ==
          driver.number_of_nodes());

  //! [collection_ids_and_locations_usage]
  for (const auto& [id, pid] : all_indices) {
    const auto it =
        driver.collection_ids_and_locations<CollectionComponent>().find(id);
    REQUIRE(it !=
            driver.collection_ids_and_locations<CollectionComponent>().end());
    CHECK(it->second.process_id == pid);
  }
  //! [collection_ids_and_locations_usage]

  //! [collection_ids_on_processes_usage]
  for (const auto& [id, pid] : all_indices) {
    const std::vector<std::uint64_t>& elements_on_pid =
        driver.collection_ids_on_process<CollectionComponent>(pid);
    CHECK(std::count(elements_on_pid.begin(), elements_on_pid.end(), id) == 1);
  }
  //! [collection_ids_on_processes_usage]

  // Check exceptions that should be thrown before we call insert_barrier()
  CHECK_THROWS_WITH_AS(
      driver.launch_threads(),
      "Cannot call driver.launch_threads() while still in Insert mode. You "
      "must first call driver.insert_barrier() on all processes.",
      rts::Exception);
  {
    const std::string expected_message_rtq{
        "Cannot call driver.run_to_quiescence() while still in Insert mode. "
        "You must first call driver.insert_barrier() on all processes.  "
        "Process ID: " +
        std::to_string(driver.current_node_id())};
    CHECK_THROWS_WITH_AS(driver.run_to_quiescence(),
                         expected_message_rtq.c_str(), rts::Exception);
    const std::string expected_message_invoke{
        "Cannot invoke actions while still in Insert mode. You must first call "
        "driver.insert_barrier() on all processes."};
    CHECK_THROWS_WITH_AS((driver.invoke<TestAction, RegularComponent>(
                             driver.current_node_id(), 5, 7)),
                         expected_message_invoke.c_str(), rts::Exception);
    const std::string expected_message_broadcast{
        "Cannot perform broadcasts while still in Insert mode. You must first "
        "call driver.insert_barrier() on all processes."};
    CHECK_THROWS_WITH_AS(
        (driver.broadcast<TestBroadcastAction, RegularComponent>(10, 20, 1.5)),
        expected_message_broadcast.c_str(), rts::Exception);
    const std::string expected_message_broadcast_to{
        "Cannot perform broadcast_to while still in Insert mode. You must "
        "first call driver.insert_barrier() on all processes."};
    CHECK_THROWS_WITH_AS(
        (driver.broadcast_to<TestBroadcastToAction, CollectionComponent>(
            [](const std::uint64_t index) { return index > 10; }, 6, 2.5)),
        expected_message_broadcast_to.c_str(), rts::Exception);
  }

  driver.insert_barrier();

  // Check exceptions that should be thrown before we call launch_threads()
  // but after insert_barrier()
  {
    const std::string expected_message_rtq{
        "Cannot call driver.run_to_quiescence() before "
        "driver.launch_threads(). Process ID: " +
        std::to_string(driver.current_node_id())};
    CHECK_THROWS_WITH_AS(driver.run_to_quiescence(),
                         expected_message_rtq.c_str(), rts::Exception);
    const std::string expected_message_invoke{
        "Cannot call invoke() on process " +
        std::to_string(driver.current_node_id()) +
        " because the threads have not been launched. You must "
        "first call driver.launch_threads()."};
    CHECK_THROWS_WITH_AS((driver.invoke<TestAction, RegularComponent>(
                             driver.current_node_id(), 5, 7)),
                         expected_message_invoke.c_str(), rts::Exception);
    const std::string expected_message_broadcast{
        "Cannot call broadcast() on process " +
        std::to_string(driver.current_node_id()) +
        " because the threads have not been launched. You must "
        "first call driver.launch_threads()."};
    CHECK_THROWS_WITH_AS(
        (driver.broadcast<TestBroadcastAction, RegularComponent>(10, 20, 1.5)),
        expected_message_broadcast.c_str(), rts::Exception);
    const std::string expected_message_broadcast_to{
        "Cannot call broadcast_to() on process " +
        std::to_string(driver.current_node_id()) +
        " because the threads have not been launched. You must "
        "first call driver.launch_threads()."};
    CHECK_THROWS_WITH_AS(
        (driver.broadcast_to<TestBroadcastToAction, CollectionComponent>(
            [](const std::uint64_t index) { return index > 10; }, 6, 2.5)),
        expected_message_broadcast_to.c_str(), rts::Exception);
  }

  {
    // Check that we can remove and then re-add a collection element
    const std::uint64_t element_to_remove = 44;
    driver.barrier();
    driver.remove_parallel_component_collection<CollectionComponent>(
        element_to_remove);
    driver.insert_barrier();

    REQUIRE(driver.collection_ids_and_locations<CollectionComponent>().find(
                element_to_remove) ==
            driver.collection_ids_and_locations<CollectionComponent>().end());

    {
      const std::vector<std::uint64_t>& elements_on_pid =
          driver.collection_ids_on_process<CollectionComponent>(
              all_indices.at(element_to_remove));
      CHECK(std::count(elements_on_pid.begin(), elements_on_pid.end(),
                       element_to_remove) == 0);
    }

    driver.barrier();
    driver.insert_parallel_component_collection<CollectionComponent>(
        element_to_remove, all_indices.at(element_to_remove));
    driver.insert_barrier();

    const auto it =
        driver.collection_ids_and_locations<CollectionComponent>().find(
            element_to_remove);
    REQUIRE(it !=
            driver.collection_ids_and_locations<CollectionComponent>().end());
    CHECK(it->second.process_id == all_indices.at(element_to_remove));
    {
      const std::vector<std::uint64_t>& elements_on_pid =
          driver.collection_ids_on_process<CollectionComponent>(
              all_indices.at(element_to_remove));
      CHECK(std::count(elements_on_pid.begin(), elements_on_pid.end(),
                       element_to_remove) == 1);
    }
    driver.barrier();
    {
      const std::string expected_message{
          "Unable to remove collection element 10000 from parallel component " +
          CollectionComponent::name()};
      CHECK_THROWS_WITH_AS(
          driver.remove_parallel_component_collection<CollectionComponent>(
              10000),
          expected_message.c_str(), rts::Exception);
    }
    driver.insert_barrier();
  }

  driver.launch_threads();
  // Test that we can safely stop and start threads:
  driver.run_to_quiescence();
  driver.force_threads_to_stop();
  driver.insert_barrier();
  driver.launch_threads();

  {
    const std::string expected_message_lt{
        "Threads are already active. You cannot launch threads when they are "
        "already running. Process ID: " +
        std::to_string(driver.current_node_id())};
    CHECK_THROWS_WITH_AS(driver.launch_threads(), expected_message_lt.c_str(),
                         rts::Exception);
  }

  // Test invoke for regular component
  driver.invoke<TestAction, RegularComponent>(driver.current_node_id(), 5, 7);
  // Test invoke for collection component
  if (driver.current_node_id() == 1) {
    driver.invoke<TestAction, CollectionComponent>(42ul, 3, 4);
    driver.invoke<TestAction, CollectionComponent>(43ul, 7, 9);
  }
  if (driver.current_node_id() == 0) {
    driver.invoke<TestAction, CollectionComponent>(44ul, 7, 9);
    driver.invoke<TestAction, CollectionComponent>(46ul, 2, 5);
  }

  driver.run_to_quiescence();

  // Check results for invoke
  auto* reg = rts::local_parallel_component<RegularComponent>(driver);
  CHECK(reg->last_result == 12);
  CHECK(std::get<0>(reg->last_args) == 5);
  CHECK(std::get<1>(reg->last_args) == 7);

  // Check we invoked and got correct answer.
  for (const auto& [idx, expected_a, expected_b] :
       {std::tuple{42ul, 3, 4}, {43ul, 7, 9}, {44ul, 7, 9}, {46ul, 2, 5}}) {
    const int expected_node = all_indices.at(idx);
    auto* const elem =
        rts::local_parallel_component<CollectionComponent>(driver, idx);
    if (driver.current_node_id() == expected_node) {
      REQUIRE(elem != nullptr);
      CHECK(elem->last_result == expected_a + expected_b);
      CHECK(elem->last_args == std::tuple{expected_a, expected_b});
      elem->last_result = 0;
    } else {
      CHECK(elem == nullptr);
    }
  }

  // Make sure we didn't send to other indices
  for (const uint64_t idx : {45ul, 47ul, 48ul}) {
    const auto* const elem =
        rts::local_parallel_component<CollectionComponent>(driver, idx);
    if (driver.current_node_id() == all_indices.at(idx)) {
      REQUIRE(elem != nullptr);
      CHECK(elem->last_result == 0);
      CHECK(elem->last_args == std::tuple{0, 0.0});
    } else {
      CHECK(elem == nullptr);
    }
  }
  // verify no broadcast or broadcast_to was recorded.
  for (const auto& [idx, node] : all_indices) {
    auto* const elem =
        rts::local_parallel_component<CollectionComponent>(driver, idx);
    if (driver.current_node_id() == node) {
      REQUIRE(elem != nullptr);
      CHECK(elem->last_result == 0); // If this fails, we missed a check and
                                     // reset above.
      CHECK(elem->last_broadcast_args == std::tuple{0, 0, 0.0});
      CHECK(elem->last_broadcast_to_args == std::tuple{0, 0.0});
      elem->last_result = 0;
    } else {
      CHECK(elem == nullptr);
    }
  }

  // Ensure we don't conflict with checks
  driver.barrier();

  // Test invoke for regular component with serialized data
  {
    std::vector vec{5, 7, 11, -1};
    vec.reserve(20);
    if (driver.current_node_id() == 1) {
      driver.invoke<TestAction, RegularComponent>(0, vec);
    }
    if (driver.current_node_id() == 0) {
      driver.invoke<TestAction, RegularComponent>(1, vec);
    }
  }
  // Test invoke for collection component with serialized data
  if (driver.current_node_id() == 1) {
    std::vector vec{7, 7, 11, -1};
    driver.invoke<TestAction, CollectionComponent>(42ul, vec);
    vec[0] = 13;
    driver.invoke<TestAction, CollectionComponent>(43ul, vec);
  }
  if (driver.current_node_id() == 0) {
    std::vector vec{111, 7, 11, -12};
    driver.invoke<TestAction, CollectionComponent>(44ul, vec);
    vec[0] += 11;
    driver.invoke<TestAction, CollectionComponent>(46ul, vec);
  }

  driver.run_to_quiescence();

  CHECK(reg->last_result == 22);
  CHECK(std::get<0>(reg->last_args) == 4);
  CHECK(std::get<1>(reg->last_args) == 20);

  // Check we invoked and got correct answer.
  for (const auto& [idx, expected_accum] :
       {std::tuple{42ul, 24}, {43ul, 30}, {44ul, 117}, {46ul, 128}}) {
    const int expected_node = all_indices.at(idx);
    auto* const elem =
        rts::local_parallel_component<CollectionComponent>(driver, idx);
    if (driver.current_node_id() == expected_node) {
      REQUIRE(elem != nullptr);
      CHECK(elem->last_result == expected_accum);
      CHECK(std::get<0>(elem->last_args) == 4);
      CHECK(std::get<1>(elem->last_args) == 4);
      elem->last_result = 0;
    } else {
      CHECK(elem == nullptr);
    }
  }

  // Make sure we didn't send to other indices
  for (const uint64_t idx : {45ul, 47ul, 48ul}) {
    const auto* const elem =
        rts::local_parallel_component<CollectionComponent>(driver, idx);
    if (driver.current_node_id() == all_indices.at(idx)) {
      REQUIRE(elem != nullptr);
      CHECK(elem->last_result == 0);
      CHECK(elem->last_args == std::tuple{0, 0.0});
    } else {
      CHECK(elem == nullptr);
    }
  }
  // verify no broadcast or broadcast_to was recorded.
  for (const auto& [idx, node] : all_indices) {
    auto* const elem =
        rts::local_parallel_component<CollectionComponent>(driver, idx);
    if (driver.current_node_id() == node) {
      REQUIRE(elem != nullptr);
      CHECK(elem->last_result == 0);  // If this fails, we missed a check and
                                      // reset above.
      CHECK(elem->last_broadcast_args == std::tuple{0, 0, 0.0});
      CHECK(elem->last_broadcast_to_args == std::tuple{0, 0.0});
      elem->last_result = 0;
    } else {
      CHECK(elem == nullptr);
    }
  }

  driver.barrier();

  auto test_broadcast = [&](const int from_process, auto use_vector) {
    constexpr bool use_vector_v = decltype(use_vector)::value;
    std::vector<int> vector_data{1, 3, 7, 9};
    // Test broadcast from process 0
    if (driver.current_node_id() == from_process) {
      if constexpr (use_vector_v) {
        driver.broadcast<TestBroadcastAction, RegularComponent>(
            10 + from_process, 20, 1.5, vector_data);
        driver.broadcast<TestBroadcastAction, CollectionComponent>(
            2 + from_process, 8, 3.5, vector_data);
      } else {
        driver.broadcast<TestBroadcastAction, RegularComponent>(
            10 + from_process, 20, 1.5);
        driver.broadcast<TestBroadcastAction, CollectionComponent>(
            2 + from_process, 8, 3.5);
      }
    }

    driver.run_to_quiescence();

    // 10 + 20 + 1.5 = 31.5 -> 31
    CHECK(reg->last_result ==
          (31 + from_process +
           (use_vector_v
                ? std::accumulate(vector_data.begin(), vector_data.end(), 0)
                : 0)));
    CHECK(std::get<0>(reg->last_broadcast_args) == (10 + from_process));
    CHECK(std::get<1>(reg->last_broadcast_args) == 20);
    CHECK(std::get<2>(reg->last_broadcast_args) == 1.5);

    for (const auto& [idx, node] : all_indices) {
      auto* const elem =
          rts::local_parallel_component<CollectionComponent>(driver, idx);
      if (driver.current_node_id() == node) {
        REQUIRE(elem != nullptr);
        CHECK(elem->last_result ==
              (13 + from_process +
               (use_vector_v
                    ? std::accumulate(vector_data.begin(), vector_data.end(), 0)
                    : 0)));
        CHECK(elem->last_broadcast_args ==
              std::make_tuple((2 + from_process), 8, 3.5));
        CHECK(elem->last_broadcast_to_args == std::tuple{0, 0.0});
        // Reset for next round.
        elem->last_result = 0;
        elem->last_broadcast_args = std::make_tuple(0, 0, 0.0);
      } else {
        CHECK(elem == nullptr);
      }
    }

    // Ensure we don't conflict with checks
    driver.barrier();
  };

  for (int from_pid = 0; from_pid < number_of_processes; ++from_pid) {
    test_broadcast(from_pid, std::bool_constant<false>{});
  }

  driver.barrier();

  for (int from_pid = 0; from_pid < number_of_processes; ++from_pid) {
    test_broadcast(from_pid, std::bool_constant<true>{});
  }

  auto test_broadcast_to = [&](const int from_process, auto use_vector) {
    constexpr bool use_vector_v = decltype(use_vector)::value;
    std::vector<int> vector_data{11, 13, 17, 19};
    // Test broadcast_to for collection component (only even indices)
    struct EvenPredicate {
      bool operator()(const uint64_t idx) const { return idx % 2 == 0; }
    };

    if (driver.current_node_id() == from_process) {
      if constexpr (use_vector_v) {
        driver.broadcast_to<TestBroadcastToAction, CollectionComponent>(
            EvenPredicate{}, 6 + from_process, 2.5, vector_data);
      } else {
        driver.broadcast_to<TestBroadcastToAction, CollectionComponent>(
            EvenPredicate{}, 6 + from_process, 2.5);
      }
    }

    driver.run_to_quiescence();

    // Check results for broadcast_to
    for (const auto& [idx, node] : all_indices) {
      auto* const elem =
          rts::local_parallel_component<CollectionComponent>(driver, idx);
      if (driver.current_node_id() == node) {
        REQUIRE(elem != nullptr);
        if (EvenPredicate{}(idx)) {
          CHECK(elem->last_result ==
                (8 + from_process +
                 (use_vector_v ? std::accumulate(vector_data.begin(),
                                                 vector_data.end(), 0)
                               : 0)));
          CHECK(elem->last_broadcast_args == std::tuple{0, 0, 0.0});
          CHECK(elem->last_broadcast_to_args ==
                std::tuple{(6 + from_process), 2.5});
          elem->last_result = 0;
          elem->last_broadcast_to_args = std::tuple{0, 0.0};
        } else {
          CHECK(elem->last_result == 0);
          CHECK(elem->last_broadcast_args == std::tuple{0, 0, 0.0});
          CHECK(elem->last_broadcast_to_args == std::tuple{0, 0.0});
        }
      } else {
        CHECK(elem == nullptr);
      }
    }

    // Ensure we don't conflict with checks
    driver.barrier();
  };

  for (int from_pid = 0; from_pid < number_of_processes; ++from_pid) {
    test_broadcast_to(from_pid, std::bool_constant<false>{});
  }

  for (int from_pid = 0; from_pid < number_of_processes; ++from_pid) {
    test_broadcast_to(from_pid, std::bool_constant<true>{});
  }

  driver.force_threads_to_stop();

  test_callbacks(driver);
}
}  // namespace testing
}  // namespace

MPI_TEST_CASE("DistributedTaskDriver.2Processes", 2) {
  rts::DistributedTaskDriver& driver =
      rts::create_distributed_task_driver(nullptr, nullptr, false);
  testing::test_bulk_enequeue_iterator_exceptions();
  testing::test_invoke(driver);
}

MPI_TEST_CASE("DistributedTaskDriver.InsertErrorRegular0", 2) {
  rts::DistributedTaskDriver& driver =
      rts::create_distributed_task_driver(nullptr, nullptr, false);
  if (driver.current_node_id() == 0) {
    driver.insert_parallel_component<testing::RegularComponent>();
    CHECK_THROWS_WITH_AS(
        driver.insert_barrier(),
        "Insertion error: The number of regular components is different on "
        "different processes. This means you have different "
        "rts::insert_parallel_component calls on different processes. Process "
        "0 has 1, process 1 has 0",
        rts::Exception);
    driver.barrier();
  } else {
    driver.insert_barrier();
  }
}

MPI_TEST_CASE("DistributedTaskDriver.InsertErrorRegular1", 2) {
  rts::DistributedTaskDriver& driver =
      rts::create_distributed_task_driver(nullptr, nullptr, false);
  if (driver.current_node_id() != 0) {
    driver.insert_parallel_component<testing::RegularComponent>();
    driver.insert_barrier();
  } else {
    CHECK_THROWS_WITH_AS(
        driver.insert_barrier(),
        "Insertion error: The number of regular components is different on "
        "different processes. This means you have different "
        "rts::insert_parallel_component calls on different processes. Process "
        "0 has 0, process 1 has 1",
        rts::Exception);
    driver.barrier();
  }
}

MPI_TEST_CASE("DistributedTaskDriver.InsertErrorCollection0", 2) {
  rts::DistributedTaskDriver& driver =
      rts::create_distributed_task_driver(nullptr, nullptr, false);
  if (driver.current_node_id() == 0) {
    driver.insert_parallel_component_collection<testing::CollectionComponent>(
        42ul, 0);
    CHECK_THROWS_WITH_AS(
        driver.insert_barrier(),
        "Insertion error: The number of collection components is different on "
        "different processes. This means you have different "
        "rts::insert_parallel_component_collection calls on different "
        "processes. Process 0 has 1, process 1 has 0",
        rts::Exception);
    driver.barrier();
  } else {
    driver.insert_barrier();
  }
}

MPI_TEST_CASE("DistributedTaskDriver.InsertErrorCollection1", 2) {
  rts::DistributedTaskDriver& driver =
      rts::create_distributed_task_driver(nullptr, nullptr, false);
  if (driver.current_node_id() != 0) {
    driver.insert_parallel_component_collection<testing::CollectionComponent>(
        42ul, 0);
    driver.insert_barrier();
  } else {
    CHECK_THROWS_WITH_AS(
        driver.insert_barrier(),
        "Insertion error: The number of collection components is different on "
        "different processes. This means you have different "
        "rts::insert_parallel_component_collection calls on different "
        "processes. Process 0 has 0, process 1 has 1",
        rts::Exception);
    driver.barrier();
  }
}

namespace {
struct RegularComponentLong : public rts::detail::DistributedObjectBase {
  static std::string name() { return "RegularComponentLong"; }
};
}  // namespace

MPI_TEST_CASE("DistributedTaskDriver.InsertErrorRegularName", 2) {
  rts::DistributedTaskDriver& driver =
      rts::create_distributed_task_driver(nullptr, nullptr, false);
  if (driver.current_node_id() == 0) {
    driver.insert_parallel_component<testing::RegularComponent>();
  } else {
    driver.insert_parallel_component<RegularComponentLong>();
  }

  if (driver.current_node_id() == 0) {
    CHECK_THROWS_WITH_AS(
        driver.insert_barrier(),
        "Insertion error: Regular component 0 name differs: process 0 has "
        "'RegularComponent', process 1 has 'RegularComponentLong'. You must "
        "have inserted the components in a different order on different "
        "processes.",
        rts::Exception);
    driver.barrier();
  } else {
    driver.insert_barrier();
  }
}

TEST_CASE("CompareComponentAccountingBuffers") {
  // Case A empty, B non-empty
  CHECK_THROWS_WITH_AS(
      compare_component_accounting_buffers(
          std::vector<char>{}, 100, std::vector<char>{'a', 'b', 'c'}, 200),
      "Insertion error: Process 100 has none while process 200 has non-zero.",
      Exception);

  // Case A non-empty, B empty
  CHECK_THROWS_WITH_AS(
      compare_component_accounting_buffers(std::vector<char>{'x', 'y'}, 42,
                                           std::vector<char>{}, 7),
      "Insertion error: Process 7 has none while process 42 has non-zero.",
      Exception);
}

// Test: Collection component name differs
namespace {
struct CollectionComponentLong
    : public rts::DistributedObjectCollection<CollectionComponentLong> {
  using rts_collection_index = uint64_t;
  static std::string name() { return "CollectionComponentLong"; }
};
}  // namespace

MPI_TEST_CASE("DistributedTaskDriver.InsertError_CollectionName0", 2) {
  rts::DistributedTaskDriver& driver =
      rts::create_distributed_task_driver(nullptr, nullptr, false);
  if (driver.current_node_id() == 0) {
    driver.insert_parallel_component_collection<testing::CollectionComponent>(
        42ul, 0);
  } else {
    driver.insert_parallel_component_collection<CollectionComponentLong>(42ul,
                                                                         1);
  }
  if (driver.current_node_id() == 0) {
    CHECK_THROWS_WITH_AS(
        driver.insert_barrier(),
        "Insertion error: Collection component 0 name differs: process 0 has "
        "'CollectionComponent', process 1 has 'CollectionComponentLong'",
        rts::Exception);
    driver.barrier();
  } else {
    driver.insert_barrier();
  }
}

MPI_TEST_CASE("DistributedTaskDriver.InsertError_CollectionName1", 2) {
  rts::DistributedTaskDriver& driver =
      rts::create_distributed_task_driver(nullptr, nullptr, false);
  if (driver.current_node_id() == 1) {
    driver.insert_parallel_component_collection<testing::CollectionComponent>(
        42ul, 1);
  } else {
    driver.insert_parallel_component_collection<CollectionComponentLong>(42ul,
                                                                         0);
  }
  if (driver.current_node_id() == 0) {
    CHECK_THROWS_WITH_AS(
        driver.insert_barrier(),
        "Insertion error: Collection component 0 name differs: process 0 has "
        "'CollectionComponentLong', process 1 has 'CollectionComponent'",
        rts::Exception);
    driver.barrier();
  } else {
    driver.insert_barrier();
  }
}

MPI_TEST_CASE("DistributedTaskDriver.InsertError_CollectionNumElements0", 2) {
  // Test: Collection number of elements differs
  rts::DistributedTaskDriver& driver =
      rts::create_distributed_task_driver(nullptr, nullptr, false);
  if (driver.current_node_id() == 0) {
    driver.insert_parallel_component_collection<testing::CollectionComponent>(
        42ul, 0);
    driver.insert_parallel_component_collection<testing::CollectionComponent>(
        43ul, 0);
  } else {
    driver.insert_parallel_component_collection<testing::CollectionComponent>(
        42ul, 1);
  }
  if (driver.current_node_id() == 0) {
    CHECK_THROWS_WITH_AS(
        driver.insert_barrier(),
        "Insertion error: Collection component 'CollectionComponent' number of "
        "elements differs: process 0 has 2, process 1 has 1",
        rts::Exception);
    driver.barrier();
  } else {
    driver.insert_barrier();
  }
}

MPI_TEST_CASE("DistributedTaskDriver.InsertError_CollectionNumElements1", 2) {
  // Test: Collection number of elements differs
  rts::DistributedTaskDriver& driver =
      rts::create_distributed_task_driver(nullptr, nullptr, false);
  if (driver.current_node_id() == 1) {
    driver.insert_parallel_component_collection<testing::CollectionComponent>(
        42ul, 1);
    driver.insert_parallel_component_collection<testing::CollectionComponent>(
        43ul, 1);
  } else {
    driver.insert_parallel_component_collection<testing::CollectionComponent>(
        42ul, 0);
  }
  if (driver.current_node_id() == 0) {
    CHECK_THROWS_WITH_AS(
        driver.insert_barrier(),
        "Insertion error: Collection component 'CollectionComponent' number of "
        "elements differs: process 0 has 1, process 1 has 2",
        rts::Exception);
    driver.barrier();
  } else {
    driver.insert_barrier();
  }
}

MPI_TEST_CASE("DistributedTaskDriver.InsertError_CollectionElementId0", 2) {
  // Test: Collection element ID differs
  rts::DistributedTaskDriver& driver =
      rts::create_distributed_task_driver(nullptr, nullptr, false);
  if (driver.current_node_id() == 0) {
    driver.insert_parallel_component_collection<testing::CollectionComponent>(
        43ul, 0);
    driver.insert_parallel_component_collection<testing::CollectionComponent>(
        44ul, 0);
  } else {
    driver.insert_parallel_component_collection<testing::CollectionComponent>(
        42ul, 1);
    driver.insert_parallel_component_collection<testing::CollectionComponent>(
        44ul, 1);
  }
  if (driver.current_node_id() == 0) {
    CHECK_THROWS_WITH_AS(
        driver.insert_barrier(),
        "Insertion error: Collection component 'CollectionComponent', element "
        "0 ID differs: process 0 has 43, process 1 has 42",
        rts::Exception);
    driver.barrier();
  } else {
    driver.insert_barrier();
  }
}

MPI_TEST_CASE("DistributedTaskDriver.InsertError_CollectionElementPid0", 2) {
  // Test: Collection element process ID differs
  rts::DistributedTaskDriver& driver =
      rts::create_distributed_task_driver(nullptr, nullptr, false);
  // Both insert the same indices, but on different processes
  driver.insert_parallel_component_collection<testing::CollectionComponent>(
      42ul, 0);
  driver.insert_parallel_component_collection<testing::CollectionComponent>(
      43ul, driver.current_node_id());
  if (driver.current_node_id() == 0) {
    CHECK_THROWS_WITH_AS(
        driver.insert_barrier(),
        "Insertion error: Collection component 'CollectionComponent', element "
        "1 (ID 43) is on "
        "process 0 for process 0 but on process 1 for process 1",
        rts::Exception);
    driver.barrier();
  } else {
    driver.insert_barrier();
  }
}

namespace {
/*!
 * \brief Generate all non-empty subsets of the input vector up to a maximum
 * subset size.
 *
 * This function returns all possible subsets (combinations) of the input vector
 * whose size is between 1 and max_subset_size (inclusive). The order of subsets
 * and the order of elements within each subset is not specified.
 *
 * \param input The input vector of `std::uint64_t` elements.
 * \param max_subset_size The maximum size of subsets to generate.
 * \return A vector of vectors, where each inner vector is a subset of the
 * input.
 *
 * \note The empty set is excluded. The full set is included if its size does
 * not exceed max_subset_size.
 *
 * Example:
 *   input = `{1, 2, 3}`, `max_subset_size = 2`
 *   Output: `{ {1}, {2}, {3}, {1,2}, {1,3}, {2,3} }`
 */
std::vector<std::vector<std::uint64_t>> generate_subsets(
    const std::vector<std::uint64_t>& input, std::size_t max_subset_size) {
  std::vector<std::vector<std::uint64_t>> result;
  result.reserve(input.size() * input.size());
  const std::size_t N = input.size();

  for (std::size_t subset_size = 1; subset_size <= std::min(N, max_subset_size);
       ++subset_size) {
    // Create a bitmask with 'subset_size' ones at the end
    std::vector<bool> bitmask(N - subset_size, false);
    bitmask.resize(N, true);

    do {
      std::vector<std::uint64_t> subset;
      subset.reserve(N);
      for (std::size_t i = 0; i < N; ++i) {
        if (bitmask[i]) {
          subset.push_back(input[i]);
        }
      }
      result.push_back(std::move(subset));
    } while (std::next_permutation(bitmask.begin(), bitmask.end()));
  }
  result.shrink_to_fit();
  return result;
}

void test_generate_subsets() {
  {
    INFO("generate_subsets with max_subset_size 2");
    const std::vector<std::uint64_t> input = {1, 2, 3};
    const std::size_t max_subset_size = 2;
    const auto subsets = generate_subsets(input, max_subset_size);

    std::set<std::set<std::uint64_t>> actual;
    for (const auto& subset : subsets) {
      actual.insert(std::set<std::uint64_t>(subset.begin(), subset.end()));
    }
    const std::set<std::set<std::uint64_t>> expected = {{1},    {2},    {3},
                                                        {1, 2}, {1, 3}, {2, 3}};
    CHECK(actual == expected);
  }

  {
    INFO("generate_subsets includes full set if allowed");
    const std::vector<std::uint64_t> input = {4, 5, 6};
    const std::size_t max_subset_size = 3;
    const auto subsets = generate_subsets(input, max_subset_size);

    std::set<std::set<std::uint64_t>> actual;
    for (const auto& subset : subsets) {
      actual.insert(std::set<std::uint64_t>(subset.begin(), subset.end()));
    }

    const std::set<std::set<std::uint64_t>> expected = {
        {4}, {5}, {6}, {4, 5}, {4, 6}, {5, 6}, {4, 5, 6}};

    CHECK(actual == expected);
  }

  {
    INFO("generate_subsets with max_subset_size 1");
    const std::vector<std::uint64_t> input = {7, 8};
    const std::size_t max_subset_size = 1;
    const auto subsets = generate_subsets(input, max_subset_size);

    std::set<std::set<std::uint64_t>> actual;
    for (const auto& subset : subsets) {
      actual.insert(std::set<std::uint64_t>(subset.begin(), subset.end()));
    }

    const std::set<std::set<std::uint64_t>> expected = {{7}, {8}};

    CHECK(actual == expected);
  }
}

/*!
 * \brief Extracts all keys from a given `std::unordered_map`.
 *
 * Returns a vector containing all the keys present in the input map.
 * The order of the keys in the returned vector is unspecified.
 *
 * \tparam Key   The type of the keys in the map.
 * \tparam Value The type of the values in the map.
 * \param map    The `std::unordered_map` from which to extract the keys.
 * \return       A `std::vector` containing all keys from the input map.
 */
template <class Key, class Value>
std::vector<Key> keys_of(const std::unordered_map<Key, Value>& map) {
  std::vector<Key> result;
  result.reserve(map.size());
  for (const auto& k_v : map) {
    result.push_back(k_v.first);
  }
  return result;
}

void test_keys_of() {
  {
    INFO("keys_of with int keys");
    const std::unordered_map<int, std::string> m = {
        {1, "a"}, {2, "b"}, {3, "c"}};
    const auto keys = keys_of(m);
    const std::set<int> actual(keys.begin(), keys.end());
    const std::set<int> expected = {1, 2, 3};
    CHECK(actual == expected);
  }

  {
    INFO("keys_of with uint64_t keys");
    const std::unordered_map<std::uint64_t, double> m = {{10, 1.1}, {20, 2.2}};
    const auto keys = keys_of(m);
    const std::set<std::uint64_t> actual(keys.begin(), keys.end());
    const std::set<std::uint64_t> expected = {10, 20};
    CHECK(actual == expected);
  }

  {
    INFO("keys_of with empty map");
    const std::unordered_map<int, int> m;
    const auto keys = keys_of(m);
    CHECK(keys.empty());
  }
}

template <bool IsCollection>
struct ReductionComponent1
    : public std::conditional_t<
          IsCollection,
          rts::DistributedObjectCollection<ReductionComponent1<IsCollection>>,
          rts::DistributedObject<ReductionComponent1<IsCollection>>> {
  ReductionComponent1() = default;
  ~ReductionComponent1() override = default;
  using rts_collection_index =
      std::conditional_t<IsCollection, std::uint64_t, int>;

  static std::string name() { return "ReductionComponent1"; }

  template <class Action, class... Args>
  void threaded_action(rts::DistributedTaskDriver& task_driver, Args... args) {
    Action::apply(task_driver, std::forward<Args>(args)...);
  }

  /*!
   * \brief Static map storing reduction results for each collection element.
   *
   * Maps collection indices to a pair of `(uint64_t, double)` that is set in
   * the callback of the reduction.
   */
  static std::unordered_map<rts_collection_index,
                            std::pair<std::uint64_t, double>>
      reduction_data;
  /// \brief Mutex for thread-safe access to reduction_data.
  static std::mutex reduction_data_mutex;
};

template <bool IsCollection>
std::unordered_map<
    typename ReductionComponent1<IsCollection>::rts_collection_index,
    std::pair<std::uint64_t, double>>
    ReductionComponent1<IsCollection>::reduction_data{};
template <bool IsCollection>
std::mutex ReductionComponent1<IsCollection>::reduction_data_mutex{};

template <bool BroadcastCallback, bool IsCollection>
struct StartReduction {
  using rts_index = std::conditional_t<IsCollection, std::uint64_t, int>;
  /*!
   * \brief Process ID to delay during the reduction (for testing).
   *
   * If set to a valid process ID, that process will sleep briefly before
   * participating in the reduction, to test try and catch race conditions.
   */
  static int delay_process;
  /*!
   * \brief Collection index to use for invoke callback reductions.
   *
   * Only used when BroadcastCallback is `false`. Specifies which collection
   * element should receive the invoke callback result.
   */
  static std::uint64_t invoke_index;
  /*!
   * \brief Optional pointer to a unary predicate for filtering elements.
   *
   * If set, only collection elements for which the predicate returns `true`
   * will participate in the reduction.
   */
  static std::optional<const std::function<bool(
      const std::conditional_t<IsCollection, std::uint64_t, int>&)>*>
      unary_predicate;
  /*!
   * \brief Counts the number of reductions.
   */
  static size_t number_of_reductions;
  /// \brief The delay amount in microseconds.
  static size_t delay_amount_us;

  /// \brief Functor for performing the reduction operation.
  struct MyOp {
    void operator()(std::tuple<std::uint64_t, double>& data,
                    const std::uint64_t i, const double d) {
      std::get<0>(data) += i;
      std::get<1>(data) += d;
    }

    void operator()(std::tuple<int, double>& data, const int i,
                    const double d) {
      std::get<0>(data) += i;
      std::get<1>(data) += d;
    }
  };

  struct SetResult {
    static void apply(rts::DistributedTaskDriver& /*task_driver*/,
                      const std::uint64_t my_index, const std::uint64_t i,
                      const double d) {
      std::lock_guard lock{
          ReductionComponent1<IsCollection>::reduction_data_mutex};
      ReductionComponent1<IsCollection>::reduction_data[my_index] =
          std::pair{i, d};
    }
    static void apply(rts::DistributedTaskDriver& task_driver, const int i,
                      const double d) {
      std::lock_guard lock{
          ReductionComponent1<IsCollection>::reduction_data_mutex};
      ReductionComponent1<
          IsCollection>::reduction_data[task_driver.current_node_id()] =
          std::pair{i, d};
    }
  };

  /*!
   * \brief Initiates the reduction operation for a given collection element.
   *
   * If a delay process is specified, that process sleeps briefly before
   * participating. If a predicate is set, only elements satisfying the
   * predicate participate. Depending on `BroadcastCallback`, either a broadcast
   * or invoke reduction callback is used.
   *
   * \param task_driver Reference to the `DistributedTaskDriver`.
   * \param my_index    The collection index of the element.
   */
  template <bool LocalIsCollection = IsCollection>
  static std::enable_if_t<LocalIsCollection> apply(
      rts::DistributedTaskDriver& task_driver, const std::uint64_t my_index) {
    if (task_driver.current_node_id() == 0) {
      number_of_reductions++;
    }
    if (task_driver.current_node_id() == delay_process) {
      std::this_thread::sleep_for(std::chrono::microseconds(delay_amount_us));
    }
    if (unary_predicate.has_value()) {
      if ((*(unary_predicate.value()))(my_index)) {
        task_driver.reduction_over<ReductionComponent1<IsCollection>, MyOp>(
            *(unary_predicate.value()), 100,
            BroadcastCallback
                ? rts::reduction::ReductionCallback<
                      SetResult, ReductionComponent1<IsCollection>>{}
                : rts::reduction::ReductionCallback<
                      SetResult,
                      ReductionComponent1<IsCollection>>{invoke_index},
            my_index, 2.0 * my_index);
      }
    } else {
      task_driver.reduction<ReductionComponent1<IsCollection>, MyOp>(
          100,
          BroadcastCallback
              ? rts::reduction::ReductionCallback<
                    SetResult, ReductionComponent1<IsCollection>>{}
              : rts::reduction::ReductionCallback<
                    SetResult, ReductionComponent1<IsCollection>>{invoke_index},
          my_index, 2.0 * my_index);
    }
  }

  template <bool LocalIsCollection = IsCollection>
  static std::enable_if_t<not LocalIsCollection> apply(
      rts::DistributedTaskDriver& task_driver) {
    const auto my_index = task_driver.current_node_id();
    if (task_driver.current_node_id() == 0) {
      number_of_reductions++;
    }
    if (task_driver.current_node_id() == delay_process) {
      std::this_thread::sleep_for(std::chrono::microseconds(delay_amount_us));
    }
    if (unary_predicate.has_value()) {
      if ((*(unary_predicate.value()))(my_index)) {
        task_driver.reduction_over<ReductionComponent1<IsCollection>, MyOp>(
            *(unary_predicate.value()), 100,
            BroadcastCallback
                ? rts::reduction::ReductionCallback<
                      SetResult, ReductionComponent1<IsCollection>>{}
                : rts::reduction::ReductionCallback<
                      SetResult,
                      ReductionComponent1<IsCollection>>{static_cast<rts_index>(
                      invoke_index)},
            my_index, 2.0 * my_index);
      }
    } else {
      task_driver.reduction<ReductionComponent1<IsCollection>, MyOp>(
          100,
          BroadcastCallback
              ? rts::reduction::ReductionCallback<
                    SetResult, ReductionComponent1<IsCollection>>{}
              : rts::reduction::ReductionCallback<
                    SetResult,
                    ReductionComponent1<IsCollection>>{static_cast<rts_index>(
                    invoke_index)},
          my_index, 2.0 * my_index);
    }
  }
};

template <bool BroadcastCallback, bool IsCollection>
int StartReduction<BroadcastCallback, IsCollection>::delay_process = -1;
template <bool BroadcastCallback, bool IsCollection>
std::uint64_t StartReduction<BroadcastCallback, IsCollection>::invoke_index = 0;
template <bool BroadcastCallback, bool IsCollection>
std::optional<const std::function<bool(
    const std::conditional_t<IsCollection, std::uint64_t, int>&)>*>
    StartReduction<BroadcastCallback, IsCollection>::unary_predicate =
        std::nullopt;
template <bool BroadcastCallback, bool IsCollection>
size_t StartReduction<BroadcastCallback, IsCollection>::number_of_reductions =
    0;
template <bool BroadcastCallback, bool IsCollection>
size_t StartReduction<BroadcastCallback, IsCollection>::delay_amount_us = 1000;

/*!
 * \brief Test reduction operations across multiple processes for a distributed
 * collection.
 *
 * This function tests both broadcast and invoke reduction callbacks for a
 * distributed collection component (ReductionComponent1) in a parallel
 * environment. It verifies that reduction results are correct for all
 * participating collection elements, optionally filtered by a predicate.
 *
 * The function performs the following:
 * - Computes the expected reduction result for all collection elements that
 *   satisfy the optional predicate.
 * - For each process, tests broadcast reduction callbacks, ensuring all local
 *   elements receive the correct result.
 * - For each collection element, tests invoke reduction callbacks, ensuring
 *   only the target element receives the result.
 * - Synchronizes processes using barriers and clears reduction results between
 *   tests.
 *
 * \param driver Reference to the DistributedTaskDriver managing the test.
 * \param maybe_unary_predicate Optional predicate to select which collection
 * elements participate in the reduction. If not provided, all elements
 * participate.
 */
template <bool IsCollection>
void test_reduction_n_processes_impl(
    rts::DistributedTaskDriver& driver,
    const std::optional<std::function<
        bool(const std::conditional_t<IsCollection, std::uint64_t, int>&)>>&
        maybe_unary_predicate) {
  using component = ReductionComponent1<IsCollection>;
  using Index = std::conditional_t<IsCollection, std::uint64_t, int>;
  if (maybe_unary_predicate.has_value()) {
    StartReduction<true, IsCollection>::unary_predicate =
        &maybe_unary_predicate.value();
    StartReduction<false, IsCollection>::unary_predicate =
        &maybe_unary_predicate.value();
  } else {
    StartReduction<true, IsCollection>::unary_predicate = std::nullopt;
    StartReduction<false, IsCollection>::unary_predicate = std::nullopt;
  }

  std::vector<std::unordered_set<Index>> ids_on_pid_to_use(
      static_cast<size_t>(driver.number_of_nodes()));
  std::pair<std::uint64_t, double> expected{0, 0.0};
  if constexpr (IsCollection) {
    for (const auto& [id, holder] :
         driver.collection_ids_and_locations<component>()) {
      if (not maybe_unary_predicate.has_value() or
          (maybe_unary_predicate.has_value() and
           maybe_unary_predicate.value()(
               detail::from_internal<component>(id)))) {
        expected.first += id;
        expected.second += 2.0 * id;
        ids_on_pid_to_use[static_cast<size_t>(holder.process_id)].insert(id);
      }
    }
  } else {
    for (int i = 0; i < driver.number_of_nodes(); ++i) {
      if (not maybe_unary_predicate.has_value() or
          (maybe_unary_predicate.has_value() and
           maybe_unary_predicate.value()(i))) {
        expected.first += static_cast<std::uint64_t>(i);
        expected.second += 2.0 * i;
        ids_on_pid_to_use[static_cast<size_t>(i)].insert(i);
      }
    }
  }
  driver.barrier();

  const auto broadcast_callback = [&driver,
                                   &expected](const int delay_process) {
    StartReduction<true, IsCollection>::delay_process = delay_process;
    component::reduction_data.clear();
    driver.barrier();
    if (driver.current_node_id() == 0) {
      driver.broadcast<StartReduction<true, IsCollection>, component>();
    }
    driver.run_to_quiescence();

    const auto check_id = [&driver, &expected](const auto id) -> bool {
      CAPTURE(id);
      const bool id_found =
          component::reduction_data.find(id) != component::reduction_data.end();
      CHECK(id_found);
      if (not id_found) {
        std::cout << std::string{
            "Failed with id " + std::to_string(id) + " on process " +
            std::to_string(driver.current_node_id()) + "\n"};
        return false;
      }
      CAPTURE(component::reduction_data.at(id).first);
      CAPTURE(component::reduction_data.at(id).second);
      CHECK(component::reduction_data.at(id).first == expected.first);
      CHECK(component::reduction_data.at(id).second == expected.second);
      return true;
    };

    if constexpr (IsCollection) {
      REQUIRE(
          component::reduction_data.size() ==
          driver.collection_ids_on_process<component>(driver.current_node_id())
              .size()

      );
      for (const auto& id : driver.collection_ids_on_process<component>(
               driver.current_node_id())) {
        if (not check_id(id)) {
          break;
        }
      }
    } else {
      // We always broadcast to exactly 1 "element" on the process for
      // PerProcess components.
      REQUIRE(component::reduction_data.size() == 1);
      CHECK(check_id(driver.current_node_id()));
    }
    component::reduction_data.clear();
    driver.barrier();
  };
  for (int delay_process = 1; delay_process < driver.number_of_nodes();
       ++delay_process) {
    broadcast_callback(delay_process);
  }

  const auto invoke_callback = [&driver, &expected](const int delay_process) {
    const auto check = [&delay_process, &driver, &expected](
                           const bool is_local, const auto invoke_index) {
      StartReduction<false, IsCollection>::delay_process = delay_process;
      StartReduction<false, IsCollection>::invoke_index =
          static_cast<std::uint64_t>(invoke_index);
      driver.barrier();
      if (driver.current_node_id() == 0) {
        driver.broadcast<StartReduction<false, IsCollection>, component>();
      }
      driver.run_to_quiescence();

      if (is_local) {
        REQUIRE(component::reduction_data.size() == 1);
        CHECK(component::reduction_data.at(invoke_index) == expected);
      } else {
        REQUIRE(component::reduction_data.size() == 0);
      }
      component::reduction_data.clear();
      driver.barrier();
    };

    if constexpr (IsCollection) {
      for (const auto& [invoke_index, holder] :
           driver.collection_ids_and_locations<component>()) {
        check(holder.process_id == driver.current_node_id(), invoke_index);
      }
    } else {
      for (int i = 0; i < driver.number_of_nodes(); ++i) {
        check(driver.current_node_id() == i, i);
      }
    }
  };
  for (int delay_process = 0; delay_process < driver.number_of_nodes();
       ++delay_process) {
    invoke_callback(delay_process);
  }
  driver.barrier();
}

template <bool IsCollection>
void test_reduction_over(rts::DistributedTaskDriver& driver,
                         const std::optional<size_t> max_subset_size) {
  using component = ReductionComponent1<IsCollection>;
  using Index = typename component::rts_collection_index;
  // Test over all sets of up to max_subset_size combinations of the IDs
  // contributing to the reduction. Does not test when nobody contributes
  // because that's just "no reduction happens".
  std::vector<std::uint64_t> pids(
      static_cast<size_t>(driver.number_of_nodes()));
  std::iota(pids.begin(), pids.end(), static_cast<std::uint64_t>(0));
  for (const std::vector<std::uint64_t>& subset_over : generate_subsets(
           IsCollection
               ? keys_of(driver.collection_ids_and_locations<component>())
               : pids,
           max_subset_size.value_or(
               IsCollection
                   ? driver.collection_ids_and_locations<component>().size()
                   : pids.size()))) {
    test_reduction_n_processes_impl<IsCollection>(
        driver, [&subset_over](const Index& id) -> bool {
          return std::find(subset_over.begin(), subset_over.end(), id) !=
                 subset_over.end();
        });
  }
  driver.barrier();
}

template <bool IsCollection>
void test_reduction_2_processes(rts::DistributedTaskDriver& driver) {
  using component = ReductionComponent1<IsCollection>;

  if constexpr (IsCollection) {
    driver.insert_parallel_component_collection<component>(
        static_cast<size_t>(1), 0);
    driver.insert_parallel_component_collection<component>(
        static_cast<size_t>(5), 0);

    driver.insert_parallel_component_collection<component>(
        static_cast<size_t>(3), 1);
    driver.insert_parallel_component_collection<component>(
        static_cast<size_t>(7), 1);
  } else {
    driver.insert_parallel_component<component>();
  }

  component::reduction_data.clear();

  driver.insert_barrier();

  // 2 elements on both processes or test PerProcess component
  test_reduction_n_processes_impl<IsCollection>(driver, std::nullopt);
  test_reduction_over<IsCollection>(driver, std::nullopt);

  if constexpr (IsCollection) {
    // Only 1 element on process 2
    driver.remove_parallel_component_collection<component>(
        static_cast<size_t>(3));
    driver.insert_barrier();
    test_reduction_n_processes_impl<IsCollection>(driver, std::nullopt);
    test_reduction_over<IsCollection>(driver, std::nullopt);

    // 0 elements on process 2
    driver.remove_parallel_component_collection<component>(
        static_cast<size_t>(7));
    driver.insert_barrier();
    test_reduction_n_processes_impl<IsCollection>(driver, std::nullopt);
    test_reduction_over<IsCollection>(driver, std::nullopt);

    // 1 element on process 1
    driver.remove_parallel_component_collection<component>(
        static_cast<size_t>(1));
    driver.insert_barrier();
    test_reduction_n_processes_impl<IsCollection>(driver, std::nullopt);
    test_reduction_over<IsCollection>(driver, std::nullopt);

    // Add 1 element back on process 2
    driver.insert_parallel_component_collection<component>(
        static_cast<size_t>(3), 1);
    driver.insert_barrier();
    test_reduction_n_processes_impl<IsCollection>(driver, std::nullopt);
    test_reduction_over<IsCollection>(driver, std::nullopt);

    // 1 element on process 2
    driver.remove_parallel_component_collection<component>(
        static_cast<size_t>(5));
    driver.insert_barrier();
    test_reduction_n_processes_impl<IsCollection>(driver, std::nullopt);
    test_reduction_over<IsCollection>(driver, std::nullopt);

    std::vector<std::uint64_t> all_ids{};
    all_ids.reserve(driver.collection_ids_and_locations<component>().size());
    for (const auto& ids_and_data :
         driver.collection_ids_and_locations<component>()) {
      all_ids.push_back(ids_and_data.first);
    }
    for (const std::uint64_t id : all_ids) {
      driver.remove_parallel_component_collection<component>(id);
    }
    driver.insert_barrier();
  }
}
}  // namespace

MPI_TEST_CASE("DistributedTaskDriver.Reduction1", 2) {
  // This test checks core reduction capabilities with only 2 processes to
  // provide an easy to debug environment.
  rts::DistributedTaskDriver& driver =
      rts::create_distributed_task_driver(nullptr, nullptr, false);
  if (driver.current_node_id() == 0) {
    test_generate_subsets();
    test_keys_of();
    if (driver.number_of_nodes() < 2) {
      throw Exception{"Must have at least 2 processes for this test."};
    }
  }
  driver.barrier();
  driver.launch_threads();

  test_reduction_2_processes<true>(driver);
  test_reduction_2_processes<false>(driver);

  driver.force_threads_to_stop();
}

namespace {
template <typename T>
std::string to_string(const std::vector<T>& vec) {
  std::stringstream os;
  os << "[";
  for (size_t i = 0; i < vec.size(); ++i) {
    os << vec[i];
    if (i + 1 < vec.size()) {
      os << ", ";
    }
  }
  os << "]";
  return os.str();
}
}  // namespace

MPI_TEST_CASE("DistributedTaskDriver.ExhaustiveReductions8Processes", 8) {
  // Tests that reductions where significant elements are missing in the tree
  // also work. This is a very long running test that attempts to catch any
  // and all bugs, including race conditions.
  //
  // With 8 processes we get the following structure:
  //                                0
  //                1                               2
  //        3               4               5               6
  //     7     8
  //
  // The cost of this test is set almost entirely by the delay amount since
  // the actual reductions are ridiculously fast. We generate about 16.8
  // million configurations, and so a delay amount of 100us means about 30 min
  // of runtime, assuming each configuration costs the same amount. This is
  // also not true because for each configuration we test all possible
  // permutations of element distributions. This makes the test extremely
  // expensive, but also extremely rigorous.
  const size_t delay_amount_us = 10;
  rts::DistributedTaskDriver& driver =
      rts::create_distributed_task_driver(nullptr, nullptr, false);

  driver.barrier();
  driver.insert_parallel_component<ReductionComponent1<false>>();
  driver.insert_barrier();
  driver.launch_threads();
  driver.barrier();

  if (driver.current_node_id() == 0) {
    std::cout << "Testing PerProcess.\n" << std::flush;
  }
  test_reduction_n_processes_impl<false>(driver, std::nullopt);
  test_reduction_over<false>(driver, std::nullopt);
  driver.barrier();

  constexpr bool IsCollection = true;
  StartReduction<true, IsCollection>::delay_amount_us = delay_amount_us;
  StartReduction<false, IsCollection>::delay_amount_us = delay_amount_us;
  const std::unordered_map<std::uint64_t, int> possible_elements_and_pids{
      {10, 0}, {11, 0}, {12, 0},           // value 0: 3 keys
      {20, 1}, {21, 1}, {22, 1},           // value 1: 3 keys
      {30, 2}, {31, 2},                    // value 2: 2 keys
      {40, 3}, {41, 3}, {42, 3}, {43, 3},  // value 3: 4 keys
      {50, 4}, {51, 4}, {52, 4},           // value 4: 3 keys
      {60, 5}, {61, 5}, {62, 5}, {63, 5},  // value 5: 4 keys
      {70, 6}, {71, 6}, {72, 6},           // value 6: 3 keys
      {80, 7}, {81, 7}                     // value 7: 2 keys
      // Total: 24
  };
  const std::vector<std::uint64_t> possible_elements =
      keys_of(possible_elements_and_pids);
  if (driver.current_node_id() == 0) {
    std::cout << "Generating element subsets which will take a bit...\n";
  }
  const std::vector<std::vector<std::uint64_t>> element_subsets =
      generate_subsets(possible_elements, possible_elements.size());
  driver.barrier();
  if (driver.current_node_id() == 0) {
    std::cout << "Generated " << element_subsets.size()
              << " element subsets.\n";
  }
  ReductionComponent1<IsCollection>::reduction_data.clear();
  driver.barrier();

  for (size_t i = 0; i < element_subsets.size(); ++i) {
    const std::vector<std::uint64_t>& elements_in_test_iteration =
        element_subsets[i];
    if (driver.current_node_id() == 0) {
      std::cout << "Performing test " << (i + 1) << "/"
                << element_subsets.size() << " over "
                << to_string(elements_in_test_iteration) << "\n"
                << std::flush;
    }
    driver.barrier();
    for (const std::uint64_t id : elements_in_test_iteration) {
      driver.insert_parallel_component_collection<
          ReductionComponent1<IsCollection>>(id,
                                             possible_elements_and_pids.at(id));
    }
    driver.insert_barrier();

    test_reduction_n_processes_impl<IsCollection>(driver, std::nullopt);
    test_reduction_over<IsCollection>(driver, std::nullopt);

    driver.barrier();
    for (const std::uint64_t id : elements_in_test_iteration) {
      driver.remove_parallel_component_collection<
          ReductionComponent1<IsCollection>>(id);
    }
    driver.insert_barrier();
  }

  driver.force_threads_to_stop();
  if (driver.current_node_id() == 0) {
    std::cout << "We performed "
              << (StartReduction<true, IsCollection>::number_of_reductions +
                  StartReduction<false, IsCollection>::number_of_reductions)
              << " reductions in total.\n";
  }
}
}  // namespace rts
#endif
