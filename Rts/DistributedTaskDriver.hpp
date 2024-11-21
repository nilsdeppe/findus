// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstdint>
#include <memory>
#include <mpi.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "Rts/DistributedObjectBase.hpp"
#include "Rts/MessageHeader.hpp"
#include "Rts/MpiException.hpp"
#include "Rts/ThreadPool.hpp"

namespace rts {
template <typename ParallelComponent>
static uint32_t class_index_counter = 0;

template <typename ParallelComponent>
uint32_t class_index() {
  static uint32_t index = (++class_index_counter<ParallelComponent>);
  return index;
}

template <typename ParallelComponent, typename Action, typename... Args>
static uint32_t function_index_counter = 0;

template <typename ParallelComponent, typename Action, typename... Args>
uint32_t function_index() {
  static uint32_t index =
      (++(function_index_counter<ParallelComponent, Action, Args...>));
  return index;
}

class DistributedTaskDriver {
 public:
  using ThreadPool_t = rts::ThreadPool<MessageHeader, int>;

  // We can only have one DistributedTaskDriver per execution, so intentionally
  // disable semantics.
  DistributedTaskDriver() = delete;
  DistributedTaskDriver(const DistributedTaskDriver& other) = delete;
  DistributedTaskDriver& operator=(const DistributedTaskDriver& other) = delete;
  DistributedTaskDriver(DistributedTaskDriver&& other) = delete;
  DistributedTaskDriver& operator=(DistributedTaskDriver&& other) = delete;
  ~DistributedTaskDriver() = default;

  void send_data(const int target_node,
                 std::unique_ptr<char[]> message_buffer) {
    if (target_node == my_node_id_) {
    } else {
      // TODO: MPI send
    }
  }

  ActionState invoke_action_on_distributed_object(
      const MessageHeader& message_header, const uint32_t thread_id) {
    return distributed_objects_[message_header.class_index]
                               [message_header.element_index]
                                   .invoke_action(
                                       message_header.function_index,
                                       message_header.serialized_data);
  }

 private:
  // The DistributedTaskDriver can only be created using the
  // create_distributed_task_driver() function.
  DistributedTaskDriver(int* argc, char** argv[], bool initialize_mpi = true);

  /// \brief Create the DistributedTaskDriver::the_driver object that can be
  /// used to globally access the task driver.
  friend void create_distributed_task_driver(int* argc, char** argv[]);

  static std::string mpi_threading_to_string(const int mpi_threading);

  struct DistributedOjectClassHolder {
    std::unordered_map<uint32_t, std::unique_ptr<DistributedObjectBase>>
        objects;
    std::string name;

    DistributedObjectBase& operator[](const uint32_t index) {
      // TODO: try-catch blocks
      return *(objects.at(index));
    }
    const DistributedObjectBase& operator[](const uint32_t index) const {
      // TODO: try-catch blocks
      return *(objects.at(index));
    }
  };

  MPI_Comm rts_comm_{};
  bool initialize_mpi_{false};
  bool mpi_supports_multithreading_{false};
  int number_of_nodes_{0};
  int my_node_id_{std::numeric_limits<int>::max()};
  int number_of_threads_{0};
  int mpi_version_{0};
  int mpi_subversion_{0};

  std::unique_ptr<ThreadPool_t> thread_pool_{};
  std::vector<DistributedOjectClassHolder> distributed_objects_;

  // std::vector<std::pair<uint32_t, BLAH>> available_message_buffers_{};
  // // This is a MPSC use-case. We need one data structure to transfer
  // ownership
  // // to the driver, and then can just have a vector that the driver uses.
  // VECTOR<std::pair<MPI_Request, std::unique_ptr<char>>>
  //     in_use_message_buffers_{};
};

static const std::unique_ptr<DistributedTaskDriver> task_driver = nullptr;

/// \cond
void create_distributed_task_driver(int* argc, char** argv[]);
/// \endcond
}  // namespace rts
