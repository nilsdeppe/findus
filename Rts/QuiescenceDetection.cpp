// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/QuiescenceDetection.hpp"

#include <algorithm>
#include <limits>
#include <mpi.h>
#include <numeric>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "Rts/Exceptions/Mpi.hpp"
#include "Rts/Exceptions/Qd.hpp"
#include "Rts/MessageTags.hpp"
#include "Rts/ParentAndChildren.hpp"

namespace rts::qd {
bool Local::is_quiescent(const std::int64_t total_number_of_threads) {
  if (phase == 1) {
    if (number_of_idle_threads.load(std::memory_order_acquire) !=
        total_number_of_threads) {
      return false;
    }
    if (const std::int64_t message_count = number_of_messages_sent.load(
            std::memory_order::memory_order_acquire);
        // sends == receives
        message_count == number_of_messages_processed.load(
                             std::memory_order::memory_order_acquire)) {
      previous_count = message_count;
      phase = 2;
      return false;
    }
    // sends != receives
    return false;
  } else if (phase == 2) {
    if (number_of_idle_threads.load(std::memory_order_acquire) !=
        total_number_of_threads) {
      phase = 1;
      return false;
    }

    if (const std::int64_t message_count = number_of_messages_sent.load(
            std::memory_order::memory_order_acquire);
        // sends == receives
        message_count == number_of_messages_processed.load(
                             std::memory_order::memory_order_acquire)
        // AND previous_count == current_count
        and previous_count == message_count) {
      return true;
    }
    // sends != receives or (previous_count != current_count)
    //
    // Our previous hope for quiescence failed, go back to phase 1.
    phase = 1;
    return false;
  }
  throw std::runtime_error{
      "The only valid local quiescence detection phases are 1 and 2, but "
      "have the value " +
      std::to_string(phase)};
}

Global::Global() = default;
Global::Global(const Global&) = default;
Global& Global::operator=(const Global&) = default;
Global::Global(Global&&) = default;
Global& Global::operator=(Global&&) = default;
Global::~Global() = default;

Global::Global(const int my_process, const int total_processes,
               const size_t max_simultaneous_qds) {
  const rts::detail::ParentAndChildren pnc =
      rts::detail::parent_and_children(my_process, total_processes);
  self_process_ = pnc.self_process_id;
  parent_process_ = pnc.parent_process_id;
  child_left_process_ = pnc.left_process_id;
  child_right_process_ = pnc.right_process_id;
  max_simultaneous_qds_ = max_simultaneous_qds;
}

bool Global::leaf() const {
  return child_left_process_ == -1 and child_right_process_ == -1;
}

bool Global::root() const { return parent_process_ == -1; }

void Global::reset() {
  terminate_ = 0;
  broadcast_left_child_request_ = std::nullopt;
  broadcast_right_child_request_ = std::nullopt;
}

void Global::safe_reset() {
  if (terminate_ != 1) {
    throw QdException{"Expected terminate to be set to 1 but is set to " +
                      std::to_string(terminate_) + " on process " +
                      std::to_string(self_process_)};
  }
  if (not accum_data_.empty()) {
    throw QdException{
        "Expected all up message accumulations to be cleaned up but have " +
        std::to_string(accum_data_.size()) + " still waiting on process " +
        std::to_string(self_process_)};
  }
  if (not down_messages_.empty()) {
    throw QdException{"Expected to have no down messages ongoing but have " +
                      std::to_string(down_messages_.size()) + " on process " +
                      std::to_string(self_process_)};
  }
  if (not up_messages_.empty()) {
    throw QdException{"Expected to have no up messages ongoing but have " +
                      std::to_string(up_messages_.size()) + " on process " +
                      std::to_string(self_process_)};
  }
  if (child_left_process_ != -1 and
      not broadcast_left_child_request_.has_value()) {
    throw QdException{"Expected to have an MPI request for the left child " +
                      std::to_string(child_left_process_) + " on process " +
                      std::to_string(self_process_)};
  }
  if (child_right_process_ != -1 and
      not broadcast_right_child_request_.has_value()) {
    throw QdException{"Expected to have an MPI request for the right child " +
                      std::to_string(child_left_process_) + " on process " +
                      std::to_string(self_process_)};
  }
  reset();
}

bool Global::check(MPI_Comm& comm) {
  // First check for edge case of only 1 process.
  int number_of_nodes = 0;
  if (MPI_Comm_size(comm, &number_of_nodes) != MPI_SUCCESS) {
    throw MpiException(
        "Failed to get the number of nodes in the ToyRTS communicator.");
  }
  if (number_of_nodes == 1) {
    terminate_ = 1;
  }

  if (terminate_ == 1) {
    return true;
  }

  // Check if we receive have received the termination.
  if (not root()) {
    int flag{0};
    if (const auto mpi_result =
            MPI_Iprobe(parent_process_, message_tags::quiescence_broadcast,
                       comm, &flag, MPI_STATUS_IGNORE);
        mpi_result != MPI_SUCCESS) {
      throw MpiException{
          "Failed to Iprobe for termination broadcast messages from parent "
          "process " +
          std::to_string(parent_process_) + " on process " +
          std::to_string(self_process_)};
    }
    if (static_cast<bool>(flag)) {
      if (const auto mpi_result = MPI_Recv(
              &terminate_, 1, MPI_INT32_T, parent_process_,
              message_tags::quiescence_broadcast, comm, MPI_STATUS_IGNORE);
          mpi_result != MPI_SUCCESS) {
        throw MpiException{
            "Failed to Recv for termination broadcast message from parent "
            "process " +
            std::to_string(parent_process_) + " on process " +
            std::to_string(self_process_)};
      }
      if (terminate_ != 1) {
        throw QdException{
            "The expected value for the integer sent during termination is 1 "
            "but process " +
            std::to_string(self_process_) + " received the value " +
            std::to_string(terminate_) + " from parent process " +
            std::to_string(parent_process_)};
      }
      send_quiescence_broadcast_to(comm, broadcast_left_child_request_,
                                   child_left_process_);
      send_quiescence_broadcast_to(comm, broadcast_right_child_request_,
                                   child_right_process_);
      return true;
    }
  }

  // Run the multi-process QD algorithm.
  //
  // The up_traversal function calls down_traversal if there is currently no up
  // traversal.
  up_traversal(comm);

  return terminate_ == 1 ? true : false;
}

void Global::wait_for_broadcast() {
  if (broadcast_left_child_request_.has_value()) {
    if (const auto mpi_result =
            MPI_Wait(&broadcast_left_child_request_.value(), MPI_STATUS_IGNORE);
        mpi_result != MPI_SUCCESS) {
      throw MpiException{
          "Failed to start MPI wait for child broadcast with child process " +
          std::to_string(child_left_process_) + " on process " +
          std::to_string(self_process_)};
    }
  }
  if (broadcast_right_child_request_.has_value()) {
    if (const auto mpi_result = MPI_Wait(
            &broadcast_right_child_request_.value(), MPI_STATUS_IGNORE);
        mpi_result != MPI_SUCCESS) {
      throw MpiException{
          "Failed to start MPI wait for child broadcast with child process " +
          std::to_string(child_right_process_) + " on process " +
          std::to_string(self_process_)};
    }
  }
  cleanup_down_messages();
  cleanup_up_messages();
}

void Global::update_last_regular_message_sweep_number(
    const std::uint64_t sweep_number_from_regular_message) {
  last_regular_message_sweep_number_ = std::max(
      sweep_number_from_regular_message, last_regular_message_sweep_number_);
}

void Global::increment_sends() { ++local_sends_; }

void Global::increment_processed() { ++local_processed_; }

namespace {
std::int64_t safe_add(const std::int64_t a, const std::int64_t b) {
  return (a == std::numeric_limits<std::int64_t>::max() or
          b == std::numeric_limits<std::int64_t>::max())
             ? std::numeric_limits<std::int64_t>::max()
             : (a + b);
}
}  // namespace

void Global::down_traversal(MPI_Comm& comm) {
  // Clean up completed down sends.
  cleanup_down_messages();

  // The sending algorithm.
  if (root()) {
    // If we already have a lot of QD messages in flight, don't start another
    // one.
    //
    // We also currently limit ourselves to at most 1 QD at a time. We could
    // relax this in the future if we find it necessary.
    if (root_down_sweep_ > local_sweep_number_) {
      return;
    }
    root_down_sweep_ = local_sweep_number_ + 1;
    down_messages_.emplace_back(
        DownData{Message::create(false, root_down_sweep_, 0)});
    send_down_to_children(comm, down_messages_.back());
    return;
  }

  int flag{0};
  if (const auto mpi_result =
          MPI_Iprobe(parent_process_, message_tags::quiescence_down, comm,
                     &flag, MPI_STATUS_IGNORE);
      mpi_result != MPI_SUCCESS) {
    throw MpiException{
        "Failed to Iprobe for down messages from parent process " +
        std::to_string(parent_process_) + " on process " +
        std::to_string(self_process_)};
  }
  if (static_cast<bool>(flag)) {
    auto receive_msg = Message::create(false, 0, 0);
    if (const auto mpi_result =
            MPI_Recv(&receive_msg, sizeof(Message), MPI_BYTE, parent_process_,
                     message_tags::quiescence_down, comm, MPI_STATUS_IGNORE);
        mpi_result != MPI_SUCCESS) {
      throw MpiException{
          "Failed to receive for down messages from parent process " +
          std::to_string(parent_process_) + " on process " +
          std::to_string(self_process_)};
    }
    if (not Message::down_traversal(receive_msg)) {
      throw QdException{
          "Expected a down message but received an up message on process " +
          std::to_string(self_process_) + " from parent process " +
          std::to_string(parent_process_)};
    }

    if (not leaf()) {
      down_messages_.emplace_back(DownData{receive_msg});
      send_down_to_children(comm, down_messages_.back());
    } else {
      const std::int64_t local_send_minus_receive =
          local_sends_ - local_processed_;
      up_messages_.emplace_back(UpData{Message::create(
          true, Message::sweep_number(receive_msg), local_send_minus_receive)});
      if (const auto mpi_result =
              MPI_Isend(&up_messages_.back().message, sizeof(Message), MPI_BYTE,
                        parent_process_, message_tags::quiescence_up, comm,
                        &up_messages_.back().parent_request);
          mpi_result != MPI_SUCCESS) {
        throw MpiException{"Failed to Isend up message to parent process " +
                           std::to_string(parent_process_) + " on process " +
                           std::to_string(self_process_)};
      }
    }
  }
}

void Global::send_down_to_children(MPI_Comm& comm, DownData& down_data) {
  if (child_left_process_ != -1) {
    if (const auto mpi_result = MPI_Isend(
            &down_data.message, sizeof(Message), MPI_BYTE, child_left_process_,
            message_tags::quiescence_down, comm, &down_data.child_left_request);
        mpi_result != MPI_SUCCESS) {
      throw MpiException{"Failed to Isend down message to left child process " +
                         std::to_string(child_left_process_) + " on process " +
                         std::to_string(self_process_)};
    }
  }
  if (child_right_process_ != -1) {
    if (const auto mpi_result =
            MPI_Isend(&down_data.message, sizeof(Message), MPI_BYTE,
                      child_right_process_, message_tags::quiescence_down, comm,
                      &down_data.child_right_request);
        mpi_result != MPI_SUCCESS) {
      throw MpiException{
          "Failed to Isend down message to right child process " +
          std::to_string(child_right_process_) + " on process " +
          std::to_string(self_process_)};
    }
  }
}

void Global::cleanup_down_messages() {
  if (not down_messages_.empty()) {
    auto erase_it = std::remove_if(
        down_messages_.begin(), down_messages_.end(),
        [process = this->self_process_,
         child_left_process = this->child_left_process_,
         child_right_process =
             this->child_right_process_](DownData& down_data) {
          if (not Message::down_traversal(down_data.message)) {
            throw QdException{
                "Found an up message while cleaning up down messages on "
                "process " +
                std::to_string(process)};
          }

          int left_flag{0};
          if (child_left_process != -1) {
            if (const auto mpi_result =
                    MPI_Request_get_status(down_data.child_left_request,
                                           &left_flag, MPI_STATUS_IGNORE);
                mpi_result != MPI_SUCCESS) {
              throw MpiException{
                  "Failed to get status of a left child down message."};
            }
          }
          int right_flag{0};
          if (child_right_process != -1) {
            if (const auto mpi_result =
                    MPI_Request_get_status(down_data.child_right_request,
                                           &right_flag, MPI_STATUS_IGNORE);
                mpi_result != MPI_SUCCESS) {
              throw MpiException{
                  "Failed to get status of a right child down message."};
            }
          }

          if ((static_cast<bool>(left_flag) or child_left_process == -1) and
              (static_cast<bool>(right_flag) or child_right_process == -1)) {
            if (child_left_process != -1) {
              if (const auto mpi_result =
                      MPI_Request_free(&down_data.child_left_request);
                  mpi_result != MPI_SUCCESS) {
                throw MpiException{
                    "Failed to call MPI_Request_free on a left child down "
                    "message."};
              }
            }
            if (child_right_process != -1) {
              if (const auto mpi_result =
                      MPI_Request_free(&down_data.child_right_request);
                  mpi_result != MPI_SUCCESS) {
                throw MpiException{
                    "Failed to call MPI_Request_free on a right child down "
                    "message."};
              }
            }
            return true;
          }
          return false;
        });
    down_messages_.erase(erase_it, down_messages_.end());
  }
}

void Global::up_traversal(MPI_Comm& comm) {
  // Clean up completed up sends.
  //
  // This also needs to happen on the leaf nodes because they start the up
  // traversal.
  cleanup_up_messages();

  if (leaf()) {
    down_traversal(comm);
    return;
  }
  receive_up_messages_from(comm, child_left_process_);
  receive_up_messages_from(comm, child_right_process_);

  // Copy the data out so we can remove it from the map.
  if (accum_data_.empty()) {
    down_traversal(comm);
    return;
  }
  auto it = accum_data_.begin();
  const auto [sweep_number, accum_data] = *it;
  if (accum_data.number_of_children_received_from !=
      ((child_left_process_ == -1 ? 0 : 1) +
       (child_right_process_ == -1 ? 0 : 1))) {
    return;
  }
  // Remove the current data we are processing from the map.
  accum_data_.erase(it);
  const std::int64_t local_count =
      (last_regular_message_sweep_number_ > local_sweep_number_)
          ? std::numeric_limits<std::int64_t>::max()
          : local_sends_ - local_processed_;
  const std::int64_t global_count =
      safe_add(local_count, accum_data.send_minus_processed);
  if (not root()) {
    up_messages_.emplace_back(
        UpData{Message::create(true, sweep_number, global_count)});
    if (const auto mpi_result =
            MPI_Isend(&up_messages_.back().message, sizeof(Message), MPI_BYTE,
                      parent_process_, message_tags::quiescence_up, comm,
                      &up_messages_.back().parent_request);
        mpi_result != MPI_SUCCESS) {
      throw MpiException{"Failed to Isend up message to parent process " +
                         std::to_string(parent_process_) + " on process " +
                         std::to_string(self_process_)};
    }
    local_sweep_number_ = sweep_number;
  } else {
    if (global_count == 0) {
      terminate_ = 1;
      send_quiescence_broadcast_to(comm, broadcast_left_child_request_,
                                   child_left_process_);
      send_quiescence_broadcast_to(comm, broadcast_right_child_request_,
                                   child_right_process_);
    } else {
      down_traversal(comm);
    }
  }
}

void Global::receive_up_messages_from(MPI_Comm& comm, const int recv_process) {
  int flag{0};
  do {
    if (const auto mpi_result =
            MPI_Iprobe(recv_process, message_tags::quiescence_up, comm, &flag,
                       MPI_STATUS_IGNORE);
        mpi_result != MPI_SUCCESS) {
      throw MpiException{
          "Failed to Iprobe for up messages from child process " +
          std::to_string(recv_process) + " on process " +
          std::to_string(self_process_)};
    }
    if (static_cast<bool>(flag)) {
      auto receive_msg = Message::create(false, 0, 0);
      if (const auto mpi_result =
              MPI_Recv(&receive_msg, sizeof(Message), MPI_BYTE, recv_process,
                       message_tags::quiescence_up, comm, MPI_STATUS_IGNORE);
          mpi_result != MPI_SUCCESS) {
        throw MpiException{
            "Failed to receive for up messages from child process " +
            std::to_string(recv_process) + " on process " +
            std::to_string(self_process_)};
      }
      if (not Message::up_traversal(receive_msg)) {
        throw QdException{
            "Expected an up message but received a down message on process " +
            std::to_string(self_process_) + " from child process " +
            std::to_string(recv_process)};
      }
      // Record the message info for later use.
      AccumulationData& accum = accum_data_[Message::sweep_number(receive_msg)];
      accum.send_minus_processed =
          safe_add(accum.send_minus_processed, receive_msg.count);
      ++accum.number_of_children_received_from;
      if (accum.number_of_children_received_from > 2) {
        throw QdException{
            "Expected at most 2 up messages for sweep number " +
            std::to_string(Message::sweep_number(receive_msg)) +
            " but received " +
            std::to_string(accum.number_of_children_received_from) +
            " on process " + std::to_string(self_process_) +
            ". Last receive from process " + std::to_string(recv_process)};
      }
    }
  } while (static_cast<bool>(flag));
}

void Global::cleanup_up_messages() {
  if (not up_messages_.empty()) {
    auto erase_it = std::remove_if(
        up_messages_.begin(), up_messages_.end(),
        [process = this->self_process_](UpData& up_data) {
          if (not Message::up_traversal(up_data.message)) {
            throw QdException{
                "Found a down message while cleaning up messages on process " +
                std::to_string(process)};
          }

          int parent_flag{0};
          if (const auto mpi_result = MPI_Request_get_status(
                  up_data.parent_request, &parent_flag, MPI_STATUS_IGNORE);
              mpi_result != MPI_SUCCESS) {
            throw MpiException{
                "Failed to get status of a left child down message."};
          }

          if (static_cast<bool>(parent_flag)) {
            if (const auto mpi_result =
                    MPI_Request_free(&up_data.parent_request);
                mpi_result != MPI_SUCCESS) {
              throw MpiException{
                  "Failed to call MPI_Request_free on a left child down "
                  "message."};
            }
            return true;
          }
          return false;
        });
    up_messages_.erase(erase_it, up_messages_.end());
  }
}

void Global::send_quiescence_broadcast_to(
    MPI_Comm& comm, std::optional<MPI_Request>& broadcast_child_request,
    const int child_process) {
  if (child_process == -1) {
    broadcast_child_request = std::nullopt;
    return;
  }
  broadcast_child_request = MPI_Request{};
  if (const auto mpi_result =
          MPI_Isend(&terminate_, 1, MPI_INT32_T, child_process,
                    message_tags::quiescence_broadcast, comm,
                    &broadcast_child_request.value());
      mpi_result != MPI_SUCCESS) {
    throw MpiException{"Failed to perform MPI_Isend from process " +
                       std::to_string(self_process_) + " to process " +
                       std::to_string(child_process) +
                       " for quiescence detection broadcast."};
  }
}
}  // namespace rts::qd
