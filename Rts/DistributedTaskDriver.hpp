// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstdint>
#include <memory>
#include <mpi.h>
#include <string>
#include <unordered_map>
#include <variant>
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

  /*!
   * \brief Insert a parallel component into the `DistributedTaskDriver`.
   *
   * This handles registration of the distributed objects without any static
   * variables. This is because we are guaranteed that all distributed objects
   * are registered on all nodes before any node tries to retrieve one based
   * on the type-erased identifier (integer). This is different from
   * actions/member functions of the distributed objects, which can be invoked
   * in an arbitrary order on different objects and so there is no order
   * guarantee, making it necessary to use static variable initialization to
   * guarantee registration before execution.
   */
  template <class ParallelComponent, class... Args>
  void insert_parallel_component(Args&&... args);

 private:
  // The DistributedTaskDriver can only be created using the
  // create_distributed_task_driver() function.
  DistributedTaskDriver(int* argc, char** argv[], bool initialize_mpi = true);

  /// \cond
  friend void create_distributed_task_driver(int* argc, char** argv[]);

  template <class ParallelComponent>
  friend ParallelComponent* local_parallel_component(
      DistributedTaskDriver& distributed_task_driver);

  template <class ParallelComponent, class IndexType>
  friend ParallelComponent* local_parallel_component(
      DistributedTaskDriver& distributed_task_driver,
      const IndexType& user_index);
  /// \endcond

  static std::string mpi_threading_to_string(const int mpi_threading);

  // The anchor function is used to compute relative pointers to member
  // functions that invoke actions.
  void anchor();

  /*!
   * \brief The type used to store each distributed object or collection.
   *
   * We use a `std::variant` of `std::unique_ptr` so that it is clear if we
   * should be indexing into a collection or not. Essentially, this is used to
   * maximize the chance of catching subtle errors since indexing a magic
   * number in the map is not guaranteed to be safe.
   */
  struct DistributedOjectClassHolder {
    std::variant<
        std::unique_ptr<DistributedObjectBase>,
        std::unordered_map<uint64_t, std::unique_ptr<DistributedObjectBase>>>
        objects;
    std::string name;
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

template <class ParallelComponent, class... Args>
void DistributedTaskDriver::insert_parallel_component(Args&&... args) {
  static_assert(
      not rts::is_collection_v<ParallelComponent>,
      "To insert into a collection use insert_parallel_component_collection");
  const auto index = rts::detail::distributed_object_index<ParallelComponent>();
  if (index < distributed_objects_.size()) {
    throw Exception(
        "Inserting a parallel component that was already inserted with index " +
        std::to_string(index) + " and name " + ParallelComponent::name());
  }
  distributed_objects_.emplace_back(
      std::unique_ptr<DistributedObjectBase>{
          std::make_unique<ParallelComponent>(std::forward<Args>(args)...)},
      ParallelComponent::name());
  if (index + 1 != distributed_objects_.size()) {
    throw Exception("The index " + std::to_string(index) +
                    " of the parallel component " + ParallelComponent::name() +
                    " that was computed by the function "
                    "distributed_object_index does not match the entry of the "
                    "distributed_objects_ vector " +
                    std::to_string(distributed_objects_.size() - 1) +
                    " on MPI rank " + std::to_string(my_node_id_));
  }
}

/// \brief Retrieve a pointer to the local parallel component.
///
/// If the pointer is null then the object could not be retrieved. This can
/// never occur in practice.
template <class ParallelComponent>
ParallelComponent* local_parallel_component(
    DistributedTaskDriver& distributed_task_driver) {
  static_assert(not rts::is_collection_v<ParallelComponent>);
  const auto object_index =
      detail::distributed_object_index<ParallelComponent>();
  if (object_index >= distributed_task_driver.distributed_objects_.size()) {
    throw rts::Exception{"Requested distributed object with index " +
                         std::to_string(object_index) + " and name " +
                         ParallelComponent::name() + " was never inserted."};
  }
  return dynamic_cast<ParallelComponent*>(
      std::get<0>(distributed_task_driver.distributed_objects_[object_index])
          .get());
}

/// \brief Retrieve a pointer to the local parallel component of a specific
/// member of a collection.
///
/// If the pointer is null then the object could not be retrieved because the
/// requested element is not on this node.
template <class ParallelComponent, class IndexType>
ParallelComponent* local_parallel_component(
    DistributedTaskDriver& distributed_task_driver,
    const IndexType& user_index) {
  static_assert(rts::is_collection_v<ParallelComponent>);
  static_assert(sizeof(IndexType) == sizeof(uint64_t));
  const auto object_index =
      detail::distributed_object_index<ParallelComponent>();
  if (object_index >= distributed_task_driver.distributed_objects_.size()) {
    throw rts::Exception{"Requested distributed object with index " +
                         std::to_string(object_index) + " and name " +
                         ParallelComponent::name() + " was never inserted."};
  }
  const uint64_t collection_index = std::hash<IndexType>{}(user_index);
  auto& object_collection =
      std::get<1>(distributed_task_driver.distributed_objects_[object_index]);
  auto& object_it = object_collection.find(collection_index);
  if (object_it == object_collection.end()) {
    return nullptr;
  }
  return dynamic_cast<ParallelComponent*>(object_it->second.get());
}

/// \cond
static const std::unique_ptr<DistributedTaskDriver> task_driver = nullptr;
/// \endcond

/// \brief Create the DistributedTaskDriver::the_driver object that can be
/// used to globally access the task driver.
void create_distributed_task_driver(int* argc, char** argv[]);
}  // namespace rts
