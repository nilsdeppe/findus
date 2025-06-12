// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstdint>
#include <memory>
#include <mpi.h>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "Rts/DistributedObjectBase.hpp"
#include "Rts/Exceptions/Exception.hpp"
#include "Rts/IsCollection.hpp"
#include "Rts/MessageHeader.hpp"
#include "Rts/ThreadPool.hpp"

namespace rts {
namespace detail {
extern uint32_t distributed_object_index_counter;

template <typename ParallelComponent>
uint32_t distributed_object_index() {
  static uint32_t index = (distributed_object_index_counter++);
  return index;
}
}  // namespace detail

class DistributedTaskDriver {
 public:
  struct Message_t {
    std::unique_ptr<char[]> message{nullptr};

    static bool execute(rts::ThreadPool<Message, int>& pool, uint32_t thread_id,
                        Message& message,
                        DistributedTaskDriver* distributed_task_driver) {
      throw Exception("Not yet implemented...");
      // TODO: Invoke message on the task driver.
      return true;
    }
  };
  using ThreadPool_t = rts::ThreadPool<Message_t, DistributedTaskDriver*>;
  /// \brief The type used for storing the incoming MPI messages while the NIC
  /// is receiving the data.
  ///
  /// Once a receive is complete, the messages need to be moved to the task
  /// queue.
  using IncomingMpiMessages_t = std::vector<std::tuple<MPI_Request, Message_t>>;
  /// \brief The type used for storing the outgoming MPI messages while the NIC
  /// is sending the data.
  ///
  /// Once a send is complete, the messages need to be freed.
  using OutgoingMpiMessages_t =
      std::vector<std::tuple<std::optional<MPI_Request>, Message_t>>;

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

  /// \brief Wait for the all MPI ranks in the RTS communicator
  ///
  /// Should be used after
  void insert_barrier() const;

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

  /*!
   * \brief Insert an element of a parallel component collection into the
   * `DistributedTaskDriver`.
   *
   * This must be called for all elements in the collection on all nodes
   * because the `DistributedTaskDriver` keeps track of where different
   * elements are to make communication easier for users.
   *
   * See `insert_parallel_component()` for details about registration.
   */
  template <class ParallelComponent, class IndexType, class... Args>
  void insert_parallel_component_collection(const IndexType& user_index,
                                            const int node_to_insert_on,
                                            Args&&... args);

  /// \brief Returns the number of nodes/MPI ranks being used.
  int number_of_nodes() const { return number_of_nodes_; }

  /// \brief The ID of the current node that this is invoked on.
  int current_node_id() const { return my_node_id_; }

  /// \brief The total number of threads being used, including the driver
  /// thread.
  int total_number_of_threads() const { return number_of_threads_; }

  /// \brief The MPI major version being used.
  int mpi_version() const { return mpi_version_; }

  /// \brief The MPI minor/subversion being used.
  int mpi_subversion() const { return mpi_subversion_; }

  /*!
   * \brief Provide an infinite loop to attach a debugger during startup. Useful
   * for debugging MPI runs.
   *
   * Each MPI rank prints out name `rts_pid_#_host_NAME` to the working
   * directory. This allows you to attach GDB to the running process using
   * `gdb --pid=PID`, once for each MPI rank. You must then halt the program
   * using `C-c` and then call `set var i = 7` inside GDB. Once you've done this
   * on each MPI rank, you can have each MPI rank `continue`.
   *
   * To add support for attaching to a debugger in an executable, you must add
   * `driver.attach_debugger()` to the start of the executable after you call
   * `rts::create_distributed_task_driver()`. Then, when you launch the
   * executable launch it as
   * ```shell
   * RTS_ATTACH_DEBUGGER=1 mpirun -np N ...
   * ```
   * The environment variable `RTS_ATTACH_DEBUGGER` being set tells the code to
   * allow attaching from a debugger.
   */
  void attach_debugger();

 private:
  // The DistributedTaskDriver can only be created using the
  // create_distributed_task_driver() function.
  DistributedTaskDriver(int* argc, char** argv[], bool initialize_mpi = true);

  /// \cond
  friend DistributedTaskDriver& create_distributed_task_driver(int* argc,
                                                               char** argv[]);

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
  void anchor() {}

  // Compute the threaded action member function pointer location relative to
  // the anchor() member function pointer. This is then sent to other nodes.
  template <class Action, class ParallelComponent, class... Args, size_t... Is>
  detail::MemberFunctionPtr threaded_action_relative_ptr(
      std::index_sequence<Is...> /*meta*/) {
    return {detail::to_member_function_ptr(
                &DistributedTaskDriver::template threaded_action_impl<
                    Action, ParallelComponent, Args..., Is...>) -
            detail::to_member_function_ptr(&DistributedTaskDriver::anchor)};
  }

  // Compute the threaded action member function pointer absolute address from
  // the address relative to the anchor() function.
  auto threaded_action_absolute_ptr(
      const detail::MemberFunctionPtr& theaded_action_rel_ptr) {
    return detail::from_member_function_ptr<void, DistributedTaskDriver,
                                            Message_t&>(
        {theaded_action_rel_ptr +
         detail::to_member_function_ptr(&DistributedTaskDriver::anchor)});
  }

  /// invoke_impl is invoked _by_ the thread pool on the task driver to
  /// initiate the action on the distributed action.
  template <class Action, class ParallelComponent, class... Args, size_t... Is>
  void threaded_action_impl(Message_t& message);

  /*!
   * \brief The type used to store each distributed object or collection.
   *
   * We use a `std::variant` of `std::unique_ptr` so that it is clear if we
   * should be indexing into a collection or not. Essentially, this is used to
   * maximize the chance of catching subtle errors since indexing a magic
   * number in the map is not guaranteed to be safe.
   */
  struct DistributedOjectClassHolder {
    struct CollectionHolder {
      int node_id = -1;
      std::unique_ptr<DistributedObjectBase> object = nullptr;
    };

    DistributedOjectClassHolder(
        std::unique_ptr<DistributedObjectBase> in_object, std::string in_name)
        : objects(std::move(in_object)), name(std::move(in_name)) {}

    DistributedOjectClassHolder(
        std::unordered_map<uint64_t, CollectionHolder> in_objects,
        std::string in_name)
        : objects(std::move(in_objects)), name(std::move(in_name)) {}

    using variant_t =
        std::variant<std::unique_ptr<DistributedObjectBase>,
                     std::unordered_map<uint64_t, CollectionHolder>>;
    variant_t objects;
    std::string name;
  };

  /*!
   * \brief Initiate several sends to remote distributed objects.
   *
   * At most `max_to_send` sends are initiated. These sends are done via a
   * non-blocking `MPI_Isend`.
   *
   * #### Implementation notes
   *
   * We dequeue in bulk from the `outgoing_messages_` since it is more
   * efficient than to do them individually. We do at most 10 at a time up to
   * `max_to_send`. The resulting `MPI_Request`s for the sends are stored in
   * the member variable `outgoing_mpi_messages_`, which is periodically
   * cleaned by the function `clean_outgoing_mpi_messages()`.
   *
   * This function is not threadsafe.
   */
  void initiate_sends(int max_to_send);

  /*!
   * \brief Cleans up any completed outgoing MPI messages.
   *
   * This function is not threadsafe.
   */
  void clean_outgoing_mpi_messages();

  /*!
   * \brief Initial several receives from remote distributed objects.
   *
   * At most `max_to_receive` sends are initiated. These receives are done via
   * a non-blocking `MPI_Irecv`.
   *
   * #### Implementation notes
   *
   * We initiate up to `max_to_receive` receives as `MPI_Irecv`. We first
   * probe for an incoming message from an MPI rank. If there is one then we
   * allocate sufficient memory and store the message and `MPI_Request` in the
   * member variable `incoming_mpi_messages_`.
   *
   * This function is not threadsafe.
   */
  void initiate_receives(int max_to_receive);

  /*!
   * \brief Checks for incoming MPI messages that have been received.
   *
   * The messages are added to the task pool in bulk.
   *
   * This function is not threadsafe.
   */
  void clean_incoming_mpi_messages();

  MPI_Comm rts_comm_{};
  bool initialize_mpi_{false};
  bool mpi_supports_multithreading_{false};
  int number_of_nodes_{0};
  int my_node_id_{std::numeric_limits<int>::max()};
  int number_of_threads_{0};
  int mpi_version_{0};
  int mpi_subversion_{0};
  int node_id_for_receive_{0};

  std::unique_ptr<ThreadPool_t> thread_pool_{};
  std::vector<DistributedOjectClassHolder> distributed_objects_;

  IncomingMpiMessages_t incoming_mpi_messages_{};
  OutgoingMpiMessages_t outgoing_mpi_messages_{};
  moodycamel::ConcurrentQueue<std::tuple<int, Message_t>> outgoing_messages_{};
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

template <class ParallelComponent, class IndexType, class... Args>
void DistributedTaskDriver::insert_parallel_component_collection(
    const IndexType& user_index, const int node_to_insert_on, Args&&... args) {
  static_assert(
      rts::is_collection_v<ParallelComponent>,
      "To insert into a collection use insert_parallel_component_collection");
  const auto index = rts::detail::distributed_object_index<ParallelComponent>();
  using Map =
      std::variant_alternative_t<1, DistributedOjectClassHolder::variant_t>;
  if (index == distributed_objects_.size()) {
    distributed_objects_.emplace_back(
        Map{},
        ParallelComponent::name());
  }
  if (index + 1 != distributed_objects_.size()) {
    throw Exception("The index " + std::to_string(index) +
                    " of the parallel component " + ParallelComponent::name() +
                    " that was computed by the function "
                    "distributed_object_index does not match the entry of the "
                    "distributed_objects_ vector " +
                    std::to_string(distributed_objects_.size() - 1) +
                    " on MPI rank " + std::to_string(my_node_id_));
  }
  Map& collection = std::get<1>(distributed_objects_[index].objects);
  const auto collection_index = std::hash<IndexType>{}(user_index);
  if (collection.find(collection_index) != collection.end()) {
    std::stringstream ss;
    ss << user_index;
    const std::string index_name = ss.str();
    throw Exception("Inserting an already existing collection index " +
                    index_name + " into the collection " +
                    ParallelComponent::name());
  }

  if (node_to_insert_on == my_node_id_) {
    collection.emplace(std::pair{
        collection_index,
        DistributedOjectClassHolder::CollectionHolder{
            node_to_insert_on, std::unique_ptr<DistributedObjectBase>{
                                   std::make_unique<ParallelComponent>(
                                       std::forward<Args>(args)...)}}});
  } else if (node_to_insert_on >= number_of_nodes_) {
    throw Exception("Cannot insert collection " + ParallelComponent::name() +
                    " into node " + std::to_string(node_to_insert_on) +
                    " because we only have " +
                    std::to_string(number_of_nodes_) + " nodes.\n");
  } else {
    // Insert for tracking which node this collection element is on.
    collection.emplace(
        std::pair{collection_index,
                  DistributedOjectClassHolder::CollectionHolder{
                      node_to_insert_on,
                      std::unique_ptr<DistributedObjectBase>{nullptr}}});
  }
}

template <class Action, class ParallelComponent, class... Args, size_t... Is>
void DistributedTaskDriver::threaded_action_impl(Message_t& message) {
  static_assert(sizeof...(Args) == sizeof...(Is));
  MessageHeader* header =
      reinterpret_cast<MessageHeader*>(message.message.get());
  if (header->distributed_object_index >= distributed_objects_.size()) {
    throw rts::Exception{"Requested distributed object with index " +
                         std::to_string(header->distributed_object_index) +
                         " but only have " +
                         std::to_string(distributed_objects_.size())};
  }

  std::tuple<Args...> args{};
  (void)std::initializer_list<char>{[&args, &message]() {
    auto& t = std::get<Is>(args);
    // TODO: deserialize
  }()...};
  if constexpr (rts::is_collection_v<ParallelComponent>) {
    dynamic_cast<ParallelComponent&>(
        *std::get<1>(
             distributed_objects_[header->distributed_object_index].objects)
             .at(header->collection_index).object)
        .template threaded_action<Action>(*this,
                                          std::move(std::get<Is>(args))...);
  } else {
    dynamic_cast<ParallelComponent&>(
        *std::get<0>(
            distributed_objects_[header->distributed_object_index].objects))
        .template threaded_action<Action>(*this,
                                          std::move(std::get<Is>(args))...);
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
DistributedTaskDriver& create_distributed_task_driver(int* argc, char** argv[]);
}  // namespace rts
