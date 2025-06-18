// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/DistributedTaskDriver.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mpi.h>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <utility>
#include <vector>

#include "Rts/Exceptions/Exception.hpp"
#include "Rts/Exceptions/Mpi.hpp"
#include "Rts/HardwareInfo.hpp"
#include "Rts/MessageTags.hpp"
#include "Rts/ParentAndChildren.hpp"

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
      static_cast<uint32_t>(number_of_threads_), 1, this);
  if (current_node_id() == 0) {
    const hardware_info::CpuInfo cpu_info = hardware_info::cpu_info();
    std::printf(
        "rts: Hardware info from process 0:\n"
        "rts:   Number of processors:       %9d\n"
        "rts:   Number of NUMA nodes:       %9d\n"
        "rts:   Number of cores:            %9d\n"
        "rts:   Number of hardware threads: %9d\n"
        "rts:   L1 cache size (kB):         %9d\n"
        "rts:   L2 cache size (kB):         %9d\n"
        "rts:   L3 cache size (kB):         %9d\n",
        cpu_info.number_of_processors, cpu_info.number_of_numa_nodes,
        cpu_info.number_of_cores, cpu_info.number_of_processing_units,
        static_cast<int>(hardware_info::cache_info(1).size) / 1024,
        static_cast<int>(hardware_info::cache_info(2).size) / 1024,
        static_cast<int>(hardware_info::cache_info(3).size) / 1024);
  }


  parent_and_children_ =
      detail::parent_and_children(current_node_id(), number_of_nodes());

  // Set global quiescence detection bookkeeping.
  global_qd_ = qd::Global{current_node_id(), number_of_nodes(), 10};
}

DistributedTaskDriver::~DistributedTaskDriver() noexcept {
  if (const auto mpi_result = MPI_Comm_free(&rts_comm_);
      mpi_result != MPI_SUCCESS) {
    std::cout << "Failed to free RTS communicator.\n" << std::flush;
  }
  if (initialize_mpi_) {
    MPI_Finalize();
  }
}

void DistributedTaskDriver::launch_threads(
    const std::optional<uint32_t> thread_for_logging) {
  thread_pool_->launch_threads(thread_for_logging);
}

void DistributedTaskDriver::force_threads_to_stop() { thread_pool_->stop(); }

bool DistributedTaskDriver::is_locally_quiescent() {
  return thread_pool_->is_quiescent();
}

void DistributedTaskDriver::insert_barrier() const { MPI_Barrier(rts_comm_); }

void DistributedTaskDriver::run_to_quiescence(const int max_to_receive,
                                              const int max_to_send) {
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
          global_qd_.wait_for_broadcast();
          global_qd_.safe_reset();
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

void DistributedTaskDriver::anchor() {}

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
  if (env_enable_parallel_debug != nullptr) {
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
          throw Exception{"Cannot request to debug on a node ID (" +
                          std::to_string(node_id) +
                          ") less than -1. RTS_ATTACH_DEBUGGER is " +
                          debugger_request};
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
      std::cout
          << "Enabling attaching to a debugger. Below are the PIDs and\n"
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
}

void DistributedTaskDriver::invoke(Message_t& message,
                                   const uint32_t thread_id) {
  MessageHeader* message_header = Message_t::get_header(message);
  (this->*threaded_action_absolute_ptr(message_header->member_function_ptr))(
      message);
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
      const int dest = std::get<0>(bulk_outgoing_messages[to_send]);
      outgoing_mpi_messages_.push_back(
          std::tuple<std::optional<MPI_Request>, Message_t>{
              MPI_Request{},
              std::move(std::get<1>(bulk_outgoing_messages[to_send]))});
      if (not std::get<0>(outgoing_mpi_messages_.back()).has_value()) {
        throw Exception(
            "The outgoing MPI message's MPI_Request is not set but it should "
            "be. This is an internal error.");
      }
      MPI_Request& request = std::get<0>(outgoing_mpi_messages_.back()).value();
      Message_t& message = std::get<1>(outgoing_mpi_messages_.back());
      MessageHeader& message_header = *Message_t::get_header(message);
      const int num_bytes =
          static_cast<int>(number_of_bytes_in_message(message_header));
      if (const auto mpi_result =
              MPI_Isend(message.message.get(), num_bytes, MPI_BYTE, dest,
                        message_tags::regular, rts_comm_, &request);
          mpi_result != MPI_SUCCESS) {
        throw MpiException{"Failed to send regular message from rank " +
                           std::to_string(current_node_id()) + " to rank " +
                           std::to_string(dest) + ". MPI returned " +
                           std::to_string(mpi_result)};
      }
      global_qd_.increment_sends();
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
        Message_t{std::unique_ptr<char[]>{
            new char[static_cast<unsigned long>(message_size)]}});
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

  typename DistributedTaskDriver::Message_t operator*() {
    if (already_dereferenced) {
      throw Exception{
          "Already dereferenced the iterator and we can only dereference it "
          "once."};
    }
    already_dereferenced = true;
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
  auto received_start = std::remove_if(
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
      std::distance(received_start, incoming_mpi_messages_.end());
  if (messages_to_emplace == 0) {
    return;
  } else if (messages_to_emplace < 0) {
    throw Exception(
        "The messages to emplace should be non-negative. This is an internal "
        "bug.");
  }
  for (auto it = received_start; it != incoming_mpi_messages_.end(); ++it) {
    global_qd_.increment_processed();
    global_qd_.update_last_regular_message_sweep_number(
        Message_t::get_header(std::get<1>(*it))
            ->quiescence_detection_sweep_number);
    MPI_Request_free(&std::get<0>(*it));
  }
  thread_pool_->add_tasks(BulkEnqueueIterator{received_start},
                          static_cast<size_t>(messages_to_emplace));
  incoming_mpi_messages_.erase(received_start, incoming_mpi_messages_.end());
}

static const std::unique_ptr<DistributedTaskDriver> task_driver = nullptr;

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
  task_driver->attach_debugger();
  return *task_driver.get();
}

namespace detail {
uint32_t distributed_object_index_counter = 0;
}  // namespace detail
}  // namespace rts
