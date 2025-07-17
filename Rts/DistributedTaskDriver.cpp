// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/DistributedTaskDriver.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
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

#include "Rts/Detail/DistributedObjectIndex.hpp"
#include "Rts/Detail/GetOutput.hpp"
#include "Rts/Detail/MpiErrorMessage.hpp"
#include "Rts/Exceptions/Exception.hpp"
#include "Rts/Exceptions/Mpi.hpp"
#include "Rts/HardwareInfo.hpp"
#include "Rts/MessageHeader.hpp"
#include "Rts/MessageTags.hpp"
#include "Rts/ParentAndChildren.hpp"

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


  parent_and_children_ =
      detail::parent_and_children(current_node_id(), number_of_nodes());

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

DistributedTaskDriver::Message_t DistributedTaskDriver::copy(
    const DistributedTaskDriver::Message_t& message) const {
  const MessageHeader& message_header = *message.get_header();
  std::unique_ptr<char[]> buffer{
      new (std::align_val_t(
          std::max(alignof(MessageHeader),
                   static_cast<size_t>(message_header.data_alignment())))) char
          [message_header.number_of_bytes_in_message()]};
  std::memcpy(buffer.get(), message.message.get(),
              message_header.number_of_bytes_in_message());
  return {std::move(buffer)};
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

void DistributedTaskDriver::insert_barrier() const {
  MPI_Barrier(rts_comm_);
  const_cast<bool&>(in_insert_mode_) = false;
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
  thread_id_ = thread_id_offset_ + thread_id;
  MessageHeader* message_header = message.get_header();
  (this->*threaded_action_absolute_ptr(message_header->member_function_ptr()))(
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
    } else {
      throw Exception{
          "BroadcastTo messages not yet supported in "
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
                    const MessageHeader* header = std::get<1>(msg).get_header();
                    return header->message_type() == MessageType::Invoke;
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
        if (collection_holder.node_id != current_node_id()) {
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
      const std::uint64_t data_alignment = header.data_alignment();
      const std::uint64_t data_size =
          header.number_of_bytes_in_message() - header.data_offset();
      const std::uint32_t data_offset =
          sizeof(MessageHeader) +
          (data_alignment - sizeof(MessageHeader) % data_alignment);
      const std::uint64_t buffer_size = data_offset + data_size;
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
        Message_t local_message{
            std::unique_ptr<char[]>{new (std::align_val_t(std::max(
                alignof(MessageHeader), data_alignment))) char[buffer_size]}};
        (new (local_message.message.get()) rts::MessageHeader(
             header.member_function_ptr(), collection_index, buffer_size,
             header.distributed_object_index(), data_offset,
             header.source_process_id(), current_node_id(),
             header.quiescence_detection_sweep_number(),
             header.data_was_serialized(), MessageType::Invoke))
            ->set_data_alignment(header.data_alignment());
        memcpy(std::next(local_message.message.get(), data_offset),
               header.data_location(), data_size);

        all_tasks.push_back(std::move(local_message));
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
  return *task_driver.get();
}

namespace detail {
uint32_t distributed_object_index_counter = 0;
}  // namespace detail
}  // namespace rts

#if defined(RTS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <doctest/extensions/doctest_mpi.h>

namespace rts {
void test_copy_message(DistributedTaskDriver& driver) {
  INFO("Test Copy Message_t");
  // Setup a dummy MessageHeader
  const rts::detail::MemberFunctionPtr dummy_ptr{};
  const std::uint64_t target_collection_index = 42;
  const std::uint64_t num_bytes = sizeof(rts::MessageHeader) + 16;
  const std::uint32_t distributed_object_index = 7;
  const std::uint32_t data_offset = sizeof(rts::MessageHeader);
  const std::int32_t source_id = 1;
  const std::int32_t dest_id = 2;
  const std::uint64_t sweep = 123;
  const bool was_serialized = false;
  const rts::MessageType type = rts::MessageType::Invoke;

  // Allocate buffer for message
  std::unique_ptr<char[]> buffer(new char[num_bytes]);
  // Placement new for header
  const MessageHeader* header = new (buffer.get()) rts::MessageHeader(
      dummy_ptr, target_collection_index, num_bytes, distributed_object_index,
      data_offset, source_id, dest_id, sweep, was_serialized, type);

  // Fill payload with known pattern
  char* const payload = buffer.get() + data_offset;
  for (size_t i = 0; i < 16; ++i) {
    payload[i] = static_cast<char>(i + 10);
  }

  // Create the message
  rts::DistributedTaskDriver::Message_t message;
  message.message = std::move(buffer);

  // Copy the message
  DistributedTaskDriver::Message_t copied = driver.copy(message);

  // Check header fields
  const MessageHeader* copied_header = copied.get_header();
  CHECK((*copied_header) == (*header));

  // Check payload
  const char* const copied_payload = copied.message.get() + data_offset;
  for (size_t i = 0; i < 16; ++i) {
    CHECK(copied_payload[i] == static_cast<char>(i + 10));
  }
}

void test_bulk_enequeue_iterator_exceptions() {
  using Message_t = DistributedTaskDriver::Message_t;
  using IncomingMpiMessages_t = DistributedTaskDriver::IncomingMpiMessages_t;

  // Helper to create a Message_t with a given MessageType
  auto make_message = [](rts::MessageType type) -> Message_t {
    const std::uint64_t num_bytes = sizeof(rts::MessageHeader);
    std::unique_ptr<char[]> buffer(new char[num_bytes]);
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

MPI_TEST_CASE("DistributedTaskDriver", 2) {
  rts::DistributedTaskDriver& driver =
      rts::create_distributed_task_driver(nullptr, nullptr, false);
  test_copy_message(driver);
  test_bulk_enequeue_iterator_exceptions();
}
}  // namespace rts
#endif
